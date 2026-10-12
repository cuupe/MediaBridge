param(
    [Parameter(Mandatory=$true)][string]$QtAndroidRoot,
    [Parameter(Mandatory=$true)][string]$QtHostRoot,
    [Parameter(Mandatory=$true)][string]$AndroidSdkRoot,
    [Parameter(Mandatory=$true)][string]$AndroidNdkRoot,
    [Parameter(Mandatory=$true)][string]$JavaHome,
    [string]$QtAndroidX64Root,
    [string]$FFmpegRoot = "$PSScriptRoot/../deps/ffmpeg-android",
    [string]$BuildDirectory = "$PSScriptRoot/../build-android",
    [string]$Ninja,
    [switch]$DebugApk
)
$ErrorActionPreference='Stop'
$taskProject=(Resolve-Path "$PSScriptRoot/..").Path.Replace('\','/')
$taskQt=(Resolve-Path -LiteralPath $QtAndroidRoot).Path.Replace('\','/')
$env:JAVA_HOME=$JavaHome
$env:ANDROID_SDK_ROOT=$AndroidSdkRoot
$env:GRADLE_USER_HOME="$taskProject/deps/gradle-cache"
$taskArguments=@('-S',$taskProject,'-B',$BuildDirectory,'-G','Ninja',"-DCMAKE_TOOLCHAIN_FILE=$taskQt/lib/cmake/Qt6/qt.toolchain.cmake","-DQT_HOST_PATH=$QtHostRoot","-DANDROID_SDK_ROOT=$AndroidSdkRoot","-DANDROID_NDK_ROOT=$AndroidNdkRoot",'-DANDROID_PLATFORM=android-28','-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON',"-DFFMPEG_ANDROID_ROOT=$FFmpegRoot",'-DMEDIABRIDGE_TESTS=OFF','-DCMAKE_BUILD_TYPE=Release')
if($Ninja){$taskArguments+="-DCMAKE_MAKE_PROGRAM=$Ninja"}
if($QtAndroidX64Root){$taskArguments+=@('-DQT_ANDROID_ABIS=arm64-v8a;x86_64',"-DQT_PATH_ANDROID_ABI_x86_64=$QtAndroidX64Root")}
cmake @taskArguments
if($LASTEXITCODE -ne 0){throw 'Android configuration failed'}
cmake --build $BuildDirectory --target apk --parallel 4
if($LASTEXITCODE -ne 0){throw 'Android APK build failed'}
# Release builds produce an unsigned APK. Sign it with the release key before installing.
if($DebugApk){
    $taskGradle=Join-Path $BuildDirectory 'android-build/gradlew.bat'
    & $taskGradle --project-dir (Join-Path $BuildDirectory 'android-build') assembleDebug
    if($LASTEXITCODE -ne 0){throw 'Android debug APK build failed'}
}
