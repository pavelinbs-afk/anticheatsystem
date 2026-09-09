#!/bin/bash
# Build anticheat with CMake for CS2 linuxsteamrt64.
#
# Usage:
#   cd cs2-anticheat && bash build_cmake.sh
#   AC_NATIVE=1 bash build_cmake.sh   # local build (Ubuntu 20.04/22.04 only)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_ROOT="$(cd "$ROOT/.." && pwd)"
DEPS="${DEPS_ROOT:-$WORK_ROOT/deps}"

if [ "${AC_IN_DOCKER:-0}" = "1" ]; then
  BUILD="$ROOT/build"
else
  if [[ "$ROOT" == /mnt/* ]]; then
    BUILD="${AC_BUILD_DIR:-$HOME/.cache/anticheat/build}"
  else
    BUILD="$ROOT/build"
  fi
fi

DOCKER_IMAGE="${AC_DOCKER_IMAGE:-anticheat-cmake-builder}"

ac_host_glibc_ok() {
  if [ -f /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    case "${VERSION_ID:-}" in
      20.04|22.04) return 0 ;;
    esac
  fi
  if command -v ldd >/dev/null 2>&1; then
    local ver
    ver="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1 || true)"
    case "$ver" in
      2.3[0-5]|2.2[0-9]|2.1[0-9]|2.[0-9]) return 0 ;;
    esac
  fi
  return 1
}

if [ "${AC_NATIVE:-0}" != "1" ] && [ "${AC_IN_DOCKER:-0}" != "1" ]; then
  if ac_host_glibc_ok; then
    echo "==> Ubuntu 20.04/22.04 (or GLIBC <= 2.35) detected — native build"
    AC_NATIVE=1
  elif ! command -v docker >/dev/null 2>&1; then
    cat >&2 <<'EOF'
ERROR: Docker is required for a CS2-compatible build on this host.

Options:
  1) Install Docker, then re-run: bash build_cmake.sh
  2) Build inside Ubuntu 20.04/22.04 (WSL distro), then re-run
EOF
    exit 1
  fi
fi

if [ "${AC_NATIVE:-0}" != "1" ] && [ "${AC_IN_DOCKER:-0}" != "1" ]; then
  if ! docker image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
    echo "==> Building Docker image $DOCKER_IMAGE (one-time)"
    docker build -f "$ROOT/Dockerfile.cmake" -t "$DOCKER_IMAGE" "$ROOT"
  fi

  echo "==> Building anticheat inside Docker ($DOCKER_IMAGE)"
  docker run --rm \
    -v "$WORK_ROOT:/work" \
    -e AC_IN_DOCKER=1 \
    -e AC_NATIVE=1 \
    "$DOCKER_IMAGE" \
    bash -lc "sed -i 's/\r$//' /work/cs2-anticheat/build_cmake.sh && bash /work/cs2-anticheat/build_cmake.sh"
  exit $?
fi

mkdir -p "$DEPS"

if command -v apt-get >/dev/null 2>&1; then
  missing=()
  command -v cmake >/dev/null 2>&1 || missing+=(cmake)
  command -v git >/dev/null 2>&1 || missing+=(git)
  command -v g++ >/dev/null 2>&1 || missing+=(build-essential)
  if [ ${#missing[@]} -gt 0 ]; then
    cat >&2 <<EOF
Missing packages: ${missing[*]}

Install once:
  sudo apt-get update
  sudo apt-get install -y build-essential cmake git
EOF
    exit 1
  fi
fi

if [ "$BUILD" != "$ROOT/build" ]; then
  echo "==> Build dir (Linux fs): $BUILD"
fi

if [ ! -f "$DEPS/hl2sdk-cs2/public/tier0/dbg.h" ]; then
  echo "==> Cloning hl2sdk-cs2"
  rm -rf "$DEPS/hl2sdk-cs2"
  git clone --branch cs2 --depth 1 https://github.com/alliedmodders/hl2sdk.git "$DEPS/hl2sdk-cs2"
fi

if [ ! -f "$DEPS/metamod-source/core/ISmmPlugin.h" ]; then
  echo "==> Cloning metamod-source"
  git clone --recursive --depth 1 https://github.com/alliedmodders/metamod-source.git "$DEPS/metamod-source"
fi

chmod +x "$DEPS/hl2sdk-cs2/devtools/bin/linux/protoc" 2>/dev/null || true

cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DHL2SDK_PATH="$DEPS/hl2sdk-cs2" \
  -DMMS_PATH="$DEPS/metamod-source"

cmake --build "$BUILD" -j"$(nproc)"

SO="$BUILD/addons/anticheat/bin/linuxsteamrt64/anticheat.so"
if [ -f "$SO" ] && command -v objdump >/dev/null 2>&1; then
  MAX_GLIBC="$(objdump -T "$SO" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -u -V | tail -1 || true)"
  if [ -n "$MAX_GLIBC" ]; then
    echo "==> Max GLIBC symbol required: $MAX_GLIBC"
  fi
fi

echo "=== BUILD OK ==="
find "$BUILD/addons" -type f
if [ "$BUILD" != "$ROOT/build" ] && [ -d "$BUILD/addons" ]; then
  rm -rf "$ROOT/build"
  mkdir -p "$ROOT/build"
  cp -a "$BUILD/addons" "$ROOT/build/"
  echo "==> Copied addons -> $ROOT/build/addons"
fi
