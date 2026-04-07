#!/usr/bin/env bash
# Install all build prerequisites for titandoom on Arch Linux (and derivatives
# such as Manjaro, EndeavourOS, CachyOS, etc.).
# Run once after cloning: bash scripts/setup-archlinux.sh
set -euo pipefail

echo "==> Syncing package databases..."
sudo pacman -Sy --needed --noconfirm

echo "==> Installing build tools..."
sudo pacman -S --needed --noconfirm \
    base-devel \
    cmake \
    ninja \
    clang \
    lldb \
    git

echo "==> Installing shader tools..."
# shaderc — GLSL → SPIR-V compiler (glslc); preferred over glslangValidator
# spirv-cross is NOT needed: SDL3 converts SPIR-V → MSL internally at runtime on macOS/Metal.
sudo pacman -S --needed --noconfirm \
    shaderc

echo "==> Installing SDL3 system dependencies..."
sudo pacman -S --needed --noconfirm \
    libx11 \
    libxext \
    libxrandr \
    libxcursor \
    libxi \
    libxss \
    libxkbcommon \
    wayland \
    wayland-protocols \
    libpulse \
    alsa-lib \
    dbus \
    systemd-libs \
    mesa \
    libdrm

echo "==> Configuring git for this repository..."
git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"
# Prevent CLion's .idea/ edits (run configs, UI state) from showing
# up as uncommitted changes. Files stay tracked in git so new clones
# get the correct CMake preset config, but local modifications by
# CLion are silently ignored.
git update-index --skip-worktree .idea/workspace.xml .idea/misc.xml .idea/vcs.xml

echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
echo "  cmake --preset debug   && cmake --build --preset debug    # debug + ASan/UBSan"
echo "  cmake --preset release && cmake --build --preset release  # optimised"
