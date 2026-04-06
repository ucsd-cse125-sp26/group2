#Requires -RunAsAdministrator
# Install all build prerequisites for titandoom on Windows.
# Run once after cloning from an elevated PowerShell:
#   Set-ExecutionPolicy Bypass -Scope Process -Force
#   .\scripts\setup-windows.ps1

$ErrorActionPreference = "Stop"

Write-Host "==> Checking for Visual Studio 2022..." -ForegroundColor Cyan

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    Write-Host "    Visual Studio 2022 not found."
    Write-Host "    Please install 'Visual Studio 2022' (Community edition is free):"
    Write-Host "    https://visualstudio.microsoft.com/downloads/"
    Write-Host ""
    Write-Host "    Required workloads during install:"
    Write-Host "      - Desktop development with C++"
    Write-Host "    Optional but recommended:"
    Write-Host "      - Game development with C++"
    Write-Host ""
    Write-Host "    Re-run this script after installing VS 2022."
    exit 1
}

Write-Host "    Visual Studio found." -ForegroundColor Green

# winget — available on Windows 10 1709+ and Windows 11
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Host "winget not available. Install from the Microsoft Store (App Installer)."
    exit 1
}

Write-Host "==> Installing CMake..." -ForegroundColor Cyan
winget install --id Kitware.CMake --silent --accept-source-agreements --accept-package-agreements

Write-Host "==> Installing Ninja..." -ForegroundColor Cyan
winget install --id Ninja-build.Ninja --silent --accept-source-agreements --accept-package-agreements

Write-Host "==> Installing LLVM (clang-format, clang-tidy)..." -ForegroundColor Cyan
winget install --id LLVM.LLVM --silent --accept-source-agreements --accept-package-agreements

Write-Host "==> Installing Vulkan SDK (glslc + spirv-cross shader tools)..." -ForegroundColor Cyan
# The Vulkan SDK bundles glslc (GLSL → SPIR-V compiler) and spirv-cross
# (SPIR-V → MSL transpiler), both required by the SDL3 GPU shader build.
winget install --id KhronosGroup.VulkanSDK --silent --accept-source-agreements --accept-package-agreements

# Locate the SDK and add its Bin dir to the user's PATH so cmake can find glslc / spirv-cross.
$sdkRoot = (Get-ChildItem "C:\VulkanSDK" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -First 1).FullName
if ($sdkRoot) {
    $sdkBin = "$sdkRoot\Bin"
    $currentPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($currentPath -notlike "*VulkanSDK*") {
        [Environment]::SetEnvironmentVariable("PATH", "$sdkBin;$currentPath", "User")
        Write-Host "    Added $sdkBin to user PATH." -ForegroundColor Green
        Write-Host "    Restart your terminal (or the Developer PowerShell) for PATH changes to take effect."
    } else {
        Write-Host "    Vulkan SDK bin already in PATH." -ForegroundColor Green
    }
} else {
    Write-Host "    WARNING: Could not locate Vulkan SDK under C:\VulkanSDK." -ForegroundColor Yellow
    Write-Host "             Add its Bin directory to your PATH manually so cmake can find glslc / spirv-cross."
}

Write-Host ""
Write-Host "==> All prerequisites installed." -ForegroundColor Green
Write-Host ""
Write-Host "Build commands (run inside 'Developer PowerShell for VS 2022'):" -ForegroundColor Yellow
Write-Host "  cmake --preset debug-win && cmake --build --preset debug-win"
Write-Host "  cmake --preset release   && cmake --build --preset release"
Write-Host ""
Write-Host "You can open 'Developer PowerShell for VS 2022' from the Start Menu,"
Write-Host "or launch it with:"
Write-Host "  & '`${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1'"
