#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${ROOT}/.webbuild"
EMSDK_DIR="${BUILD_ROOT}/emsdk"
RAYLIB_DIR="${BUILD_ROOT}/raylib"
RAYLIB_TAG="5.5"
PUBLIC_DIR="${ROOT}/public"

mkdir -p "${BUILD_ROOT}" "${PUBLIC_DIR}"

prepare_git_checkout() {
  local dir="$1"
  local clone_args="$2"

  if [ -e "${dir}" ] && [ ! -d "${dir}/.git" ]; then
    rm -rf "${dir}"
  fi

  if [ ! -d "${dir}/.git" ]; then
    # shellcheck disable=SC2086
    git clone ${clone_args} "${dir}"
  fi
}

prepare_git_checkout "${EMSDK_DIR}" "--depth 1 https://github.com/emscripten-core/emsdk.git"

pushd "${EMSDK_DIR}" >/dev/null
git fetch --depth 1 origin main
git checkout -q FETCH_HEAD
./emsdk install latest
./emsdk activate latest
# shellcheck disable=SC1091
source ./emsdk_env.sh
popd >/dev/null

prepare_git_checkout "${RAYLIB_DIR}" "--depth 1 --branch ${RAYLIB_TAG} https://github.com/raysan5/raylib.git"

pushd "${RAYLIB_DIR}" >/dev/null
git fetch --depth 1 origin "refs/tags/${RAYLIB_TAG}:refs/tags/${RAYLIB_TAG}"
git checkout -q "${RAYLIB_TAG}"
popd >/dev/null

pushd "${RAYLIB_DIR}/src" >/dev/null
make PLATFORM=PLATFORM_WEB GRAPHICS=GRAPHICS_API_OPENGL_ES2 RAYLIB_LIBTYPE=STATIC -B
popd >/dev/null

rm -f "${PUBLIC_DIR}/hopf_bloch.js" "${PUBLIC_DIR}/hopf_bloch.wasm"

emcc "${ROOT}/src/hopf_fibration.cpp" \
  -std=c++17 \
  -Os \
  -Wall \
  -Wextra \
  -Wno-missing-field-initializers \
  -DPLATFORM_WEB \
  -DGRAPHICS_API_OPENGL_ES2 \
  -I"${RAYLIB_DIR}/src" \
  "${RAYLIB_DIR}/src/libraylib.a" \
  -s USE_GLFW=3 \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ASSERTIONS=0 \
  -s MODULARIZE=0 \
  -s EXPORT_NAME=Module \
  -s ENVIRONMENT=web \
  -o "${PUBLIC_DIR}/hopf_bloch.js"

node "${ROOT}/scripts/verify-public-build.mjs"
