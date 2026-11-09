#!/usr/bin/env bash
# Configure/build with CMake. Release runs the app; debug builds only; test runs CTest.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="release"
case "${1:-}" in
    --debug) MODE="debug"; shift ;;
    --test) MODE="test"; shift ;;
    --help)
        echo "Usage: ./build.sh [--debug|--test] [config.json] [output_directory]"
        exit 0 ;;
esac
if [[ "${MODE}" == "release" ]]; then
    BUILD_TYPE="Release"
    SANITIZERS="OFF"
else
    BUILD_TYPE="Debug"
    SANITIZERS="ON"
fi
BUILD_DIR="${PROJECT_ROOT}/build/${MODE}"
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DCOLLATZ_SANITIZERS="${SANITIZERS}" \
    -DCOLLATZ_WARNINGS_AS_ERRORS=ON
cmake --build "${BUILD_DIR}" --parallel
if [[ "${MODE}" == "test" ]]; then
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
elif [[ "${MODE}" == "release" ]]; then
    "${BUILD_DIR}/collatz_run" "${1:-${PROJECT_ROOT}/config.json}" "${2:-${PROJECT_ROOT}}"
else
    echo "Built: ${BUILD_DIR}/collatz_run"
fi
