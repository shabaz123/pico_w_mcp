# MS Windows PowerShell Build Script for Pi Pico / Pico W
# rev 3 - September 2026

param(
    [ValidateSet("basic", "wlan")]
    [string]$Target = "basic",

    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release",

    [string]$WifiSsid = "",

    [string]$WifiPassword = ""
)

$ErrorActionPreference = "Stop"

# Save the caller's environment.  Environment variables are process-wide, so
# changing PATH/CC/CXX in a PowerShell script would otherwise remain in the
# current PowerShell session after the script finishes.
$originalPath = $env:Path
$originalCC = $env:CC
$originalCXX = $env:CXX

try {
    # Use paths matching blink project. These settings are temporary for this
    # build and are restored in the finally block below.
    $env:CC  = "C:/DEV/vhd_mounts/msys2/msys64/mingw64/bin/gcc.exe"
    $env:CXX = "C:/DEV/vhd_mounts/msys2/msys64/mingw64/bin/g++.exe"

    $buildPaths = @(
        "C:\DEV\vhd_mounts\msys2\msys64\mingw64\bin"
        "C:\DEV\arm_gnu_toolchains\15.2.rel1\bin"
        "C:\DEV\tools\bin"
        "C:\DEV\tools\cmake\bin"
    )

    # Construct the build PATH from the original PATH, rather than repeatedly
    # prepending to the current value. This prevents duplicate entries even if
    # the script is invoked more than once in unusual/nested circumstances.
    $env:Path = (($buildPaths + ($originalPath -split ';' | Where-Object { $_ })) -join ';')

    switch ($Target) {
        "wlan" {
            $picoBoard = "pico_w"
        }

        "basic" {
            throw "This MCP project requires a Pico W. Run with -Target wlan."
        }
    }

    if ([string]::IsNullOrWhiteSpace($WifiSsid)) {
        throw "Wi-Fi SSID is required. Supply -WifiSsid `"YOUR_WIFI_NAME`"."
    }

    if ([string]::IsNullOrWhiteSpace($WifiPassword)) {
        throw "Wi-Fi password is required. Supply -WifiPassword `"YOUR_WIFI_PASSWORD`"."
    }

    # Use a separate directory for each board and configuration.
    # This avoids reusing a CMake cache configured for another board.
    $buildDir = "build-$picoBoard-$Config"

    Write-Host ""
    Write-Host "========================================"
    Write-Host " Building $Config configuration"
    Write-Host " Target board: $picoBoard"
    Write-Host "========================================"
    Write-Host ""
    Write-Host "Build directory: $buildDir"
    Write-Host ""

    Write-Host "ARM Compiler:"
    arm-none-eabi-gcc --version | Select-Object -First 1

    Write-Host "WIN Compiler for picotool:"
    & $env:CC --version | Select-Object -First 1

    Write-Host ""

    cmake `
        -S . `
        -B $buildDir `
        -G Ninja `
        "-DCMAKE_BUILD_TYPE:STRING=$Config" `
        "-DPICO_BOARD:STRING=$picoBoard" `
        "-DWIFI_SSID:STRING=$WifiSsid" `
        "-DWIFI_PASSWORD:STRING=$WifiPassword" `
        "-Dpicotool_DIR=C:\DEV\tools\picotool\picotool"

    $configureExitCode = $LASTEXITCODE
    Write-Host ""
    Write-Host "CMake configure exit code: $configureExitCode"

    if ($configureExitCode -ne 0) {
        throw "CMake configuration failed with exit code $configureExitCode."
    }

    cmake --build $buildDir
    $buildExitCode = $LASTEXITCODE

    Write-Host ""
    Write-Host "Build exit code: $buildExitCode"

    if ($buildExitCode -ne 0) {
        throw "Build failed with exit code $buildExitCode."
    }

    Write-Host "Build completed successfully."
}
finally {
    # Restore the caller's environment even if CMake, Ninja, or a compiler fails.
    $env:Path = $originalPath

    if ($null -eq $originalCC) {
        Remove-Item Env:CC -ErrorAction SilentlyContinue
    }
    else {
        $env:CC = $originalCC
    }

    if ($null -eq $originalCXX) {
        Remove-Item Env:CXX -ErrorAction SilentlyContinue
    }
    else {
        $env:CXX = $originalCXX
    }
}
