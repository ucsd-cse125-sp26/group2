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

# Homebrew — must be installed before running this script.
# Install it manually from https://brew.sh (copy the command shown there into your terminal).
# We deliberately do not auto-download-and-execute the installer here to avoid the
# curl-pipe-bash security risk (CWE-95).
if ! command -v brew &>/dev/null; then
    # Apple Silicon homebrew lands in /opt/homebrew; add it to PATH and retry.
    eval "$(/opt/homebrew/bin/brew shellenv)" 2>/dev/null || true
fi
if ! command -v brew &>/dev/null; then
    echo "ERROR: Homebrew not found." >&2
    echo "       Install it first: https://brew.sh" >&2
    echo "       Then re-run this script." >&2
    exit 1
fi
echo "==> Homebrew found: $(brew --version | head -1)"

echo "==> Installing build tools..."
# llvm@18 is pinned to match the CI version (ubuntu-24.04 / clang-format-18).
# Using the same clang-format version locally and in CI prevents formatting drift.
brew install cmake ninja llvm@18

LLVM18_BIN="$(brew --prefix llvm@18)/bin"

# Add LLVM 18 bin to PATH for the current session and for future shells.
export PATH="$LLVM18_BIN:$PATH"

SHELL_RC=""
if [ -f "$HOME/.zshrc" ]; then
    SHELL_RC="$HOME/.zshrc"
elif [ -f "$HOME/.bash_profile" ]; then
    SHELL_RC="$HOME/.bash_profile"
fi
if [ -n "$SHELL_RC" ]; then
    if ! grep -q "llvm@18" "$SHELL_RC"; then
        echo "" >> "$SHELL_RC"
        echo "# LLVM 18 — clang-format-18 / clang-tidy-18 (pinned to match CI)" >> "$SHELL_RC"
        echo "export PATH=\"$LLVM18_BIN:\$PATH\"" >> "$SHELL_RC"
        echo "==> Added llvm@18 bin to $SHELL_RC"
    fi
fi

# Expose clang-format-18 / clang-tidy-18 as versioned names so CMake and hooks find them.
ln -sf "$LLVM18_BIN/clang-format" "$LLVM18_BIN/clang-format-18" 2>/dev/null || true
ln -sf "$LLVM18_BIN/clang-tidy"   "$LLVM18_BIN/clang-tidy-18"   2>/dev/null || true

echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
echo "  cmake --preset debug   && cmake --build --preset debug    # debug + ASan/UBSan"
echo "  cmake --preset release && cmake --build --preset release  # optimised"
