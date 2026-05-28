#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/test/host/build"
CC_BIN="${CC:-cc}"

mkdir -p "${BUILD_DIR}"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/thingsboard" \
    "${ROOT_DIR}/main/thingsboard/thingsboard_client_internal.c" \
    "${ROOT_DIR}/test/host/test_thingsboard_client_internal.c" \
    -o "${BUILD_DIR}/test_thingsboard_client_internal"

"${BUILD_DIR}/test_thingsboard_client_internal"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/network/lte" \
    "${ROOT_DIR}/main/network/lte/lte_link_internal.c" \
    "${ROOT_DIR}/test/host/test_lte_link_internal.c" \
    -o "${BUILD_DIR}/test_lte_link_internal"

"${BUILD_DIR}/test_lte_link_internal"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/app" \
    -I"${ROOT_DIR}/main/thingsboard" \
    "${ROOT_DIR}/main/thingsboard/thingsboard_client_internal.c" \
    "${ROOT_DIR}/main/app/app_controller_internal.c" \
    "${ROOT_DIR}/test/host/test_app_controller_internal.c" \
    -o "${BUILD_DIR}/test_app_controller_internal"

"${BUILD_DIR}/test_app_controller_internal"

echo "host tests passed"
