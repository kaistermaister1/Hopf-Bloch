$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptRoot
$compiler = "C:/msys64/ucrt64/bin/g++.exe"
$source = Join-Path $root "src/hopf_fibration.cpp"
$buildDir = Join-Path $root "build"
$output = Join-Path $buildDir "hopf_fibration.exe"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$env:PATH = "C:/msys64/ucrt64/bin;$env:PATH"

& $compiler `
    -std=c++17 `
    -O2 `
    -Wall `
    -Wextra `
    -Wno-missing-field-initializers `
    -IC:/msys64/ucrt64/include `
    $source `
    -o $output `
    -LC:/msys64/ucrt64/lib `
    -lraylib `
    -lopengl32 `
    -lgdi32 `
    -lwinmm

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

$dlls = @("libraylib.dll", "glfw3.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")
foreach ($dll in $dlls) {
    $from = Join-Path "C:/msys64/ucrt64/bin" $dll
    if (Test-Path $from) {
        Copy-Item -LiteralPath $from -Destination $buildDir -Force
    }
}

Write-Host "Built $output"
