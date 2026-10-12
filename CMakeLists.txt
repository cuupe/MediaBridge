cmake_minimum_required(VERSION 3.24)
project(MediaBridge VERSION 2.0.3 LANGUAGES C CXX)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(MEDIABRIDGE_GUI "Build the Qt Quick application" ON)
option(MEDIABRIDGE_TESTS "Build core integration test driver" ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(FFMPEG_ANDROID_ROOT "" CACHE PATH "Per-ABI Android FFmpeg SDK directory")
if(ANDROID AND FFMPEG_ANDROID_ROOT)
    set(FFMPEG_ROOT "${FFMPEG_ANDROID_ROOT}/${CMAKE_ANDROID_ARCH_ABI}" CACHE PATH "FFmpeg SDK for active Android ABI" FORCE)
endif()
find_package(FFmpeg REQUIRED)
find_package(Threads REQUIRED)

add_library(mediabridge_core src/core/media.cpp)
target_include_directories(mediabridge_core PUBLIC include)
target_link_libraries(mediabridge_core PUBLIC FFmpeg::FFmpeg Threads::Threads)
if(MSVC)
    target_compile_options(mediabridge_core PUBLIC /EHsc)
    target_compile_options(mediabridge_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(mediabridge_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT ANDROID)
    add_executable(mediabridge_cli src/cli/main.cpp)
    set_target_properties(mediabridge_cli PROPERTIES OUTPUT_NAME mediabridge-cli)
    target_link_libraries(mediabridge_cli PRIVATE mediabridge_core)
    if(WIN32)
        target_link_libraries(mediabridge_cli PRIVATE shell32)
    endif()
    if(MSVC)
        target_compile_options(mediabridge_cli PRIVATE /utf-8)
    endif()
    if(WIN32 AND FFMPEG_ROOT)
        file(GLOB ffmpeg_dlls "${FFMPEG_ROOT}/bin/*.dll")
        foreach(dll IN LISTS ffmpeg_dlls)
            add_custom_command(TARGET mediabridge_cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:mediabridge_cli>")
        endforeach()
    endif()
    install(TARGETS mediabridge_cli RUNTIME DESTINATION bin)
endif()

if(MEDIABRIDGE_GUI)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2)
    qt_standard_project_setup(REQUIRES 6.9)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
        qt_policy(SET QTP0004 NEW)
        qt_policy(SET QTP0002 NEW)
    endif()
    qt_add_executable(MediaBridge MANUAL_FINALIZATION
        src/gui/main.cpp src/gui/controller.cpp src/gui/controller.hpp src/gui/platform_io.cpp src/gui/platform_io.hpp)
    qt_add_qml_module(MediaBridge URI MediaBridge VERSION 2.0 QML_FILES qml/Main.qml qml/NumberField.qml qml/ChoiceField.qml qml/FormInput.qml qml/FormCombo.qml qml/ToggleField.qml qml/Panel.qml qml/ActionButton.qml qml/QueuePage.qml qml/ProcessingPage.qml qml/SettingsPage.qml)
    target_link_libraries(MediaBridge PRIVATE mediabridge_core
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    if(MSVC)
        target_compile_options(MediaBridge PRIVATE /utf-8)
    endif()
    if(ANDROID)
        list(APPEND QT_ANDROID_MULTI_ABI_FORWARD_VARS FFMPEG_ANDROID_ROOT ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES)
        set_target_properties(MediaBridge PROPERTIES
            QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
            QT_QML_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml"
            QT_ANDROID_PACKAGE_NAME "org.mediabridge.converter"
            QT_ANDROID_APP_NAME "MediaBridge"
            QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
            QT_ANDROID_VERSION_CODE 5
            QT_ANDROID_MIN_SDK_VERSION 28
            QT_ANDROID_TARGET_SDK_VERSION 35)
        file(GLOB android_ffmpeg_libs "${FFMPEG_ROOT}/lib/*.so")
        set_property(TARGET MediaBridge PROPERTY QT_ANDROID_EXTRA_LIBS "${android_ffmpeg_libs}")
    elseif(WIN32)
        set_target_properties(MediaBridge PROPERTIES WIN32_EXECUTABLE TRUE)
        foreach(dll IN LISTS ffmpeg_dlls)
            add_custom_command(TARGET MediaBridge POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:MediaBridge>")
        endforeach()
    endif()
    if(MEDIABRIDGE_TESTS AND NOT ANDROID)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        target_sources(MediaBridge PRIVATE src/gui/ui_checks.cpp src/gui/ui_checks.hpp)
        target_link_libraries(MediaBridge PRIVATE Qt6::Test)
        target_compile_definitions(MediaBridge PRIVATE MB_UI_CHECKS)
        add_executable(queue_checks tests/queue_checks.cpp
            src/gui/controller.cpp src/gui/controller.hpp src/gui/platform_io.cpp src/gui/platform_io.hpp)
        set_target_properties(queue_checks PROPERTIES AUTOMOC ON)
        target_link_libraries(queue_checks PRIVATE mediabridge_core Qt6::Core Qt6::Gui)
        if(MSVC)
            target_compile_options(queue_checks PRIVATE /utf-8)
        endif()
    endif()
    qt_finalize_executable(MediaBridge)
    if(NOT ANDROID)
        install(TARGETS MediaBridge RUNTIME DESTINATION bin)
        qt_generate_deploy_qml_app_script(TARGET MediaBridge OUTPUT_SCRIPT qt_deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR)
        install(SCRIPT ${qt_deploy_script})
    endif()
endif()

if(MEDIABRIDGE_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(core_checks tests/core_checks.cpp)
    target_link_libraries(core_checks PRIVATE mediabridge_core)
    add_test(NAME core_checks COMMAND core_checks)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    find_program(FFMPEG_EXECUTABLE ffmpeg)
    find_program(FFPROBE_EXECUTABLE ffprobe)
    if(Python3_FOUND AND FFMPEG_EXECUTABLE AND FFPROBE_EXECUTABLE)
        add_test(NAME media_integration COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py"
            --cli "$<TARGET_FILE:mediabridge_cli>" --ffmpeg "${FFMPEG_EXECUTABLE}"
            --ffprobe "${FFPROBE_EXECUTABLE}" --checks "$<TARGET_FILE:core_checks>")
        set_tests_properties(media_integration PROPERTIES TIMEOUT 300)
        if(MEDIABRIDGE_GUI)
            get_filename_component(test_qt_root "${Qt6Core_DIR}/../../.." ABSOLUTE)
            add_test(NAME queue_integration COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/queue_runner.py"
                --exe "$<TARGET_FILE:queue_checks>" --qt "${test_qt_root}" --ffmpeg "${FFMPEG_EXECUTABLE}")
            add_test(NAME gui_integration COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_smoke.py"
                --exe "$<TARGET_FILE:MediaBridge>" --qt "${test_qt_root}"
                --ffmpeg "${FFMPEG_EXECUTABLE}" --ffprobe "${FFPROBE_EXECUTABLE}"
                --artifact-dir "${CMAKE_BINARY_DIR}/ui-artifacts")
            set_tests_properties(queue_integration gui_integration PROPERTIES TIMEOUT 180)
        endif()
    endif()
endif()

if(WIN32 AND ffmpeg_dlls)
    install(FILES ${ffmpeg_dlls} DESTINATION bin)
endif()

install(FILES LICENSE README.md CHANGELOG.md THIRD-PARTY-NOTICES.md DESTINATION share/mediabridge)
install(DIRECTORY licenses/ DESTINATION share/mediabridge/licenses)
