#!/usr/bin/env bash
# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

WS="${WORKDIR:-/workdir}"
cd "${WS}"

export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-/opt/zephyr-sdk-0.17.4}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"

if [[ -f "${WS}/zephyr/zephyr-env.sh" ]]; then
	# shellcheck disable=SC1091
	source "${WS}/zephyr/zephyr-env.sh"
fi

export ZEPHYR_EXTRA_MODULES="${WS}/rak3162-zephyr-bsp/modules/rak-fw${ZEPHYR_EXTRA_MODULES:+;${ZEPHYR_EXTRA_MODULES}}"

exec "$@"
