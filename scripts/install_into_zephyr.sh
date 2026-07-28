#!/usr/bin/env bash
# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0

# Install RAK3162 BSP boards/samples into an existing Zephyr tree (optional).
# Prefer Mode 2 in README: ZEPHYR_EXTRA_MODULES (no copy) when you already have Zephyr.
# Usage:
#   ./scripts/install_into_zephyr.sh /path/to/zephyr [--force] [--dry-run]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BSP_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MARKER_NAME=".rak3162_bsp_installed"
MANIFEST_NAME=".rak3162_bsp_manifest.txt"

FORCE=0
DRY_RUN=0
ZEPHYR_BASE=""

usage() {
	echo "Usage: $0 <ZEPHYR_BASE> [--force] [--dry-run]"
	echo "  Copies boards/rak3162 and samples into the Zephyr tree."
	echo "  SX1262 is onboard in the board DTS (no shield required)."
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--force) FORCE=1; shift ;;
		--dry-run) DRY_RUN=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*)
			if [[ -z "${ZEPHYR_BASE}" ]]; then
				ZEPHYR_BASE="$1"
			else
				echo "Unexpected argument: $1" >&2
				usage >&2
				exit 1
			fi
			shift
			;;
	esac
done

if [[ -z "${ZEPHYR_BASE}" ]]; then
	usage >&2
	exit 1
fi

ZEPHYR_BASE="$(cd "${ZEPHYR_BASE}" && pwd)"
MARKER="${ZEPHYR_BASE}/${MARKER_NAME}"
MANIFEST="${ZEPHYR_BASE}/${MANIFEST_NAME}"

if [[ ! -f "${ZEPHYR_BASE}/Kconfig.zephyr" ]]; then
	echo "Error: ${ZEPHYR_BASE} does not look like a Zephyr source tree" >&2
	exit 1
fi

if [[ -f "${MARKER}" && "${FORCE}" -ne 1 ]]; then
	echo "Error: BSP already installed (found ${MARKER}). Use --force to overwrite." >&2
	exit 1
fi

SRC_BOARD="${BSP_ROOT}/boards/rak3162"
DST_BOARD="${ZEPHYR_BASE}/boards/rak3162"
SRC_SAMPLE="${BSP_ROOT}/samples/hw_test"
DST_SAMPLE="${ZEPHYR_BASE}/samples/rak/hw_test"

run() {
	if [[ "${DRY_RUN}" -eq 1 ]]; then
		echo "DRY-RUN: $*"
	else
		"$@"
	fi
}

echo "Installing RAK3162 BSP into ${ZEPHYR_BASE}"
run mkdir -p "${ZEPHYR_BASE}/boards" "${ZEPHYR_BASE}/samples/rak"
run rm -rf "${DST_BOARD}" "${DST_SAMPLE}"
run cp -a "${SRC_BOARD}" "${DST_BOARD}"
run cp -a "${SRC_SAMPLE}" "${DST_SAMPLE}"

if [[ "${DRY_RUN}" -eq 0 ]]; then
	{
		echo "# RAK3162 BSP install manifest"
		echo "version=$(git -C "${BSP_ROOT}" describe --always --dirty 2>/dev/null || echo unknown)"
		echo "board=${DST_BOARD}"
		echo "sample=${DST_SAMPLE}"
		find "${DST_BOARD}" "${DST_SAMPLE}" -type f | sort
	} > "${MANIFEST}"
	echo "installed=$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "${MARKER}"
	echo "Wrote ${MARKER} and ${MANIFEST}"
fi

echo "Done. Build example:"
echo "  west build -b rak3162/nrf54l15/cpuapp samples/rak/hw_test"
echo "LoRaWAN uses Zephyr CONFIG_LORAWAN (no Semtech USP modules required)."
