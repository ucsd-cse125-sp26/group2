#!/usr/bin/env bash
# Install all build prerequisites for titandoom on macOS.
# Run once after cloning: bash scripts/setup-macos.sh
set -euo pipefail

# Xcode Command Line Tools (provides clang, clang++, clang-tidy, metal)
if ! xcode-select -p &>/dev/null; then
    echo "==> Installing Xcode Command Line Tools..."
    xcode-select --install
    echo "    Re-run this script after the installer finishes."
    exit 0
fi
echo "==> Xcode CLT already installed."

# Homebrew
if ! command -v brew &>/dev/null; then
    echo "==> Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    # Add Homebrew to PATH for Apple Silicon
    eval "$(/opt/homebrew/bin/brew shellenv)" 2>/dev/null || true
fi
echo "==> Homebrew already installed."

echo "==> Installing build tools..."
brew install cmake ninja clang-format

echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
echo "  cmake --preset debug   && cmake --build --preset debug    # debug + ASan/UBSan"
echo "  cmake --preset release && cmake --build --preset release  # optimised"
