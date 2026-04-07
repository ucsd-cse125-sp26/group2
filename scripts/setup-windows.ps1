#Requires -RunAsAdministrator
# Install all build prerequisites for titandoom on Windows.
# Run once after cloning from an elevated PowerShell:
#   Set-ExecutionPolicy Bypass -Scope Process -Force
#   .\scripts\setup-windows.ps1

$ErrorActionPreference = "Stop"

Write-Host "==> Checking for MSVC C++ build tools..." -ForegroundColor Cyan

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasMSVC = $false
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -version "[17.0,18.0)" -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
    if ($vsPath) { $hasMSVC = $true }
}

if (-not $hasMSVC) {
    Write-Host "    No VS 2022 installation with C++ tools found." -ForegroundColor Yellow
    Write-Host "    Installing Visual Studio 2022 Build Tools (free, ~2-3 GB)..." -ForegroundColor Cyan
    winget install --id Microsoft.VisualStudio.2022.BuildTools `
        --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait" `
        --silent --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    ERROR: VS Build Tools installation failed." -ForegroundColor Red
        Write-Host "    Install manually: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022"
        exit 1
    }
    Write-Host "    VS Build Tools installed." -ForegroundColor Green
} else {
    Write-Host "    MSVC C++ tools found at: $vsPath" -ForegroundColor Green
}

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
# The Vulkan SDK bundles glslc (GLSL → SPIR-V) and spirv-cross (SPIR-V → MSL),
# both required by the SDL3 GPU shader build pipeline.
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

Write-Host "==> Configuring CLion Visual Studio toolchain..." -ForegroundColor Cyan
# .idea/cmake.xml references TOOLCHAIN_NAME="Visual Studio". CLion must have a
# toolchain by that exact name in its global settings, otherwise it falls back to
# MinGW and rejects the MSVC compiler our toolchain file provides.
$clionDirs = @(Get-ChildItem "$env:APPDATA\JetBrains" -Filter "CLion*" -Directory -ErrorAction SilentlyContinue)
if ($clionDirs.Count -gt 0) {
    foreach ($clionDir in $clionDirs) {
        $optionsDir   = Join-Path $clionDir.FullName "options"
        $toolchainsXml = Join-Path $optionsDir "toolchains.xml"
        if (-not (Test-Path $optionsDir)) {
            New-Item -ItemType Directory -Path $optionsDir -Force | Out-Null
        }
        if (-not (Test-Path $toolchainsXml)) {
            @"
<?xml version="1.0" encoding="UTF-8"?>
<application>
  <component name="CPPToolchains" version="4">
    <toolchain name="Visual Studio" toolSetKind="MSVCToolSet" />
  </component>
</application>
"@ | Set-Content $toolchainsXml -Encoding UTF8
            Write-Host "    Created $toolchainsXml" -ForegroundColor Green
        } else {
            $doc = New-Object System.Xml.XmlDocument
            $doc.Load($toolchainsXml)
            if ($null -eq $doc.SelectSingleNode('//toolchain[@name="Visual Studio"]')) {
                $comp = $doc.SelectSingleNode('//component[@name="CPPToolchains"]')
                if ($null -eq $comp) {
                    $comp = $doc.CreateElement("component")
                    $comp.SetAttribute("name", "CPPToolchains")
                    $comp.SetAttribute("version", "4")
                    $doc.DocumentElement.AppendChild($comp) | Out-Null
                }
                $tc = $doc.CreateElement("toolchain")
                $tc.SetAttribute("name", "Visual Studio")
                $tc.SetAttribute("toolSetKind", "MSVCToolSet")
                # Insert first → becomes the default toolchain
                if ($comp.HasChildNodes) {
                    $comp.InsertBefore($tc, $comp.FirstChild) | Out-Null
                } else {
                    $comp.AppendChild($tc) | Out-Null
                }
                $doc.Save($toolchainsXml)
                Write-Host "    Added 'Visual Studio' toolchain to $($clionDir.Name)" -ForegroundColor Green
            } else {
                Write-Host "    'Visual Studio' toolchain already present in $($clionDir.Name)" -ForegroundColor Green
            }
        }
    }
} else {
    Write-Host "    CLion not found — run this script again after installing CLion." -ForegroundColor Yellow
    Write-Host "    Or add it manually: Settings > Build > Toolchains > + Visual Studio" -ForegroundColor Yellow
}

Write-Host "==> Configuring git for this repository..." -ForegroundColor Cyan
git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"

Write-Host ""
Write-Host "==> All prerequisites installed." -ForegroundColor Green
Write-Host ""
Write-Host "IMPORTANT: Close and reopen CLion / your terminal so PATH changes take effect." -ForegroundColor Yellow
Write-Host ""
Write-Host "IDE setup (CLion / Visual Studio / VSCode):" -ForegroundColor Cyan
Write-Host "  1. Open your IDE and select File > Open > this repo root"
Write-Host "  2. CMake presets (debug-win, release-win, relwithdebinfo-win) appear automatically"
Write-Host "  3. Select a preset and build"
Write-Host ""
Write-Host "Command-line build (any terminal):" -ForegroundColor Cyan
Write-Host "  cmake --preset debug-win   && cmake --build --preset debug-win"
Write-Host "  cmake --preset release-win && cmake --build --preset release-win"
