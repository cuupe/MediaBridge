param(
    [Parameter(Mandatory=$true)][string]$FFmpegRoot,
    [string]$QtRoot = "",
    [string]$BuildDirectory = "build-windows",
    [string]$Generator = "Visual Studio 18 2026"
)
$ErrorActionPreference = "Stop"
$project = Split-Path -Parent $PSScriptRoot
$build = Join-Path $project $BuildDirectory
$arguments = @("-S", $project, "-B", $build, "-G", $Generator,
               "-DFFMPEG_ROOT=$FFmpegRoot", "-DMEDIABRIDGE_GUI=$(-not [string]::IsNullOrEmpty($QtRoot))")
if ($QtRoot) { $arguments += "-DCMAKE_PREFIX_PATH=$QtRoot" }
& cmake @arguments
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
& cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
& cmake --install $build --config Release --prefix (Join-Path $project "dist/windows")
if ($LASTEXITCODE -ne 0) { throw "Installation failed" }
