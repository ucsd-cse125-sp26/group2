#!/usr/bin/env bash
# Install all build prerequisites for titandoom on Debian/Ubuntu.
# Run once after cloning: bash scripts/setup-linux.sh
set -euo pipefail

echo "==> Updating package lists..."
sudo apt-get update -qq

echo "==> Installing build tools..."
sudo apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    clang \
    clang-format \
    clang-tidy \
    lldb \
    git

echo "==> Installing SDL3 system dependencies..."
sudo apt-get install -y --no-install-recommends \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxi-dev \
    libxss-dev \
    libxkbcommon-dev \
    libwayland-dev \
    wayland-protocols \
    libpulse-dev \
    libasound2-dev \
    libdbus-1-dev \
    libudev-dev \
    libgl-dev \
    libgles2-mesa-dev \
    libdrm-dev \
    libgbm-dev

echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
echo "  cmake --preset debug   && cmake --build --preset debug    # debug + ASan/UBSan"
echo "  cmake --preset release && cmake --build --preset release  # optimised"
