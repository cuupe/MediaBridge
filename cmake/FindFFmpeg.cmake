# Supply a development SDK (headers + import/shared libraries), not ffmpeg.exe.
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg SDK prefix for the target platform/ABI")
if(FFMPEG_ROOT)
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h PATHS "${FFMPEG_ROOT}/include" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    foreach(component avformat avcodec avfilter swscale swresample avutil)
        find_library(FFMPEG_${component}_LIBRARY NAMES ${component}
            PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        list(APPEND required_vars FFMPEG_${component}_LIBRARY)
        list(APPEND ffmpeg_libraries "${FFMPEG_${component}_LIBRARY}")
    endforeach()
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(FFmpeg REQUIRED_VARS FFMPEG_INCLUDE_DIR ${required_vars})
    if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
        add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
        set_target_properties(FFmpeg::FFmpeg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${ffmpeg_libraries}")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat>=61 libavcodec>=61.19.100 libavfilter>=10 libavutil>=59 libswscale libswresample)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    target_link_libraries(FFmpeg::FFmpeg INTERFACE PkgConfig::FFMPEG)
    set(FFmpeg_FOUND TRUE)
endif()
