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

echo "==> Installing SDL3 system dependencies..."
sudo pacman -S --needed --noconfirm \
    libx11 \
    libxext \
    libxrandr \
    libxcursor \
    libxi \
    libxscrnsaver \
    libxkbcommon \
    wayland \
    wayland-protocols \
    libpulse \
    alsa-lib \
    dbus \
    systemd-libs \
    mesa \
    libdrm

echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
echo "  cmake --preset debug   && cmake --build --preset debug    # debug + ASan/UBSan"
echo "  cmake --preset release && cmake --build --preset release  # optimised"
