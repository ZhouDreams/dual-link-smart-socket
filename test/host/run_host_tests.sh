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
    -I"${ROOT_DIR}/main/network" \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/thingsboard" \
    "${ROOT_DIR}/test/host/test_thingsboard_client_lifecycle.c" \
    -o "${BUILD_DIR}/test_thingsboard_client_lifecycle"

"${BUILD_DIR}/test_thingsboard_client_lifecycle"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/network/lte" \
    "${ROOT_DIR}/main/network/lte/lte_link_internal.c" \
    "${ROOT_DIR}/test/host/test_lte_link_internal.c" \
    -o "${BUILD_DIR}/test_lte_link_internal"

"${BUILD_DIR}/test_lte_link_internal"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/network" \
    -I"${ROOT_DIR}/main/network/wifi" \
    "${ROOT_DIR}/main/network/wifi/wifi_link_internal.c" \
    "${ROOT_DIR}/test/host/test_wifi_link_internal.c" \
    -o "${BUILD_DIR}/test_wifi_link_internal"

"${BUILD_DIR}/test_wifi_link_internal"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/metering" \
    "${ROOT_DIR}/main/metering/metering_service_internal.c" \
    "${ROOT_DIR}/test/host/test_metering_service_internal.c" \
    -o "${BUILD_DIR}/test_metering_service_internal"

"${BUILD_DIR}/test_metering_service_internal"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/metering" \
    -I"${ROOT_DIR}/main/bl0942" \
    "${ROOT_DIR}/test/host/test_metering_service_event_flow.c" \
    -o "${BUILD_DIR}/test_metering_service_event_flow"

"${BUILD_DIR}/test_metering_service_event_flow"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/relay" \
    "${ROOT_DIR}/test/host/test_relay_event_order.c" \
    -o "${BUILD_DIR}/test_relay_event_order"

"${BUILD_DIR}/test_relay_event_order"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/app" \
    -I"${ROOT_DIR}/main/thingsboard" \
    "${ROOT_DIR}/main/thingsboard/thingsboard_client_internal.c" \
    "${ROOT_DIR}/main/app/app_controller_internal.c" \
    "${ROOT_DIR}/test/host/test_app_controller_internal.c" \
    -o "${BUILD_DIR}/test_app_controller_internal"

"${BUILD_DIR}/test_app_controller_internal"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/main/safety" \
    -I"${ROOT_DIR}/main/metering" \
    -I"${ROOT_DIR}/main/relay" \
    -I"${ROOT_DIR}/main/button" \
    -I"${ROOT_DIR}/main/bl0942" \
    -I"${ROOT_DIR}/main/platform" \
    -I"${ROOT_DIR}/main/network" \
    -I"${ROOT_DIR}/main/thingsboard" \
    -I"${ROOT_DIR}/main/display/lvgl" \
    -I"${ROOT_DIR}/main/display/tft" \
    -I"${ROOT_DIR}/main/app" \
    -I"${ROOT_DIR}/test/support" \
    "${ROOT_DIR}/test/host/test_app_controller_event_order.c" \
    -o "${BUILD_DIR}/test_app_controller_event_order"

"${BUILD_DIR}/test_app_controller_event_order"

echo "host tests passed"
