#!/usr/bin/env bash
# Install all build prerequisites for titandoom on Debian/Ubuntu.
# Run once after cloning: bash scripts/setup-linux.sh
set -euo pipefail

echo "==> Updating package lists..."
sudo apt-get update -qq

echo "==> Installing build tools..."
# clang-format-18 and clang-tidy-18 are pinned to match the CI version (ubuntu-24.04 default).
# Using the same version locally and in CI ensures formatting never drifts between the two.
sudo apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    clang \
    clang-format-18 \
    clang-tidy-18 \
    lldb \
    git

# Register the pinned versions as the default so plain 'clang-format' / 'clang-tidy' resolve to -18.
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 18
sudo update-alternatives --install /usr/bin/clang-tidy   clang-tidy   /usr/bin/clang-tidy-18   18

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
