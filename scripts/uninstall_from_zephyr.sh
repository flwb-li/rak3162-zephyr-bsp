#!/usr/bin/env bash
# Remove files previously installed by install_into_zephyr.sh.
# Usage:
#   ./scripts/uninstall_from_zephyr.sh /path/to/zephyr [--dry-run]
set -euo pipefail

FORCE_LIST=0
DRY_RUN=0
ZEPHYR_BASE=""

usage() {
	echo "Usage: $0 <ZEPHYR_BASE> [--dry-run]"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
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
MARKER="${ZEPHYR_BASE}/.rak3162_bsp_installed"
MANIFEST="${ZEPHYR_BASE}/.rak3162_bsp_manifest.txt"
DST_BOARD="${ZEPHYR_BASE}/boards/rak/rak3162"
DST_SAMPLE="${ZEPHYR_BASE}/samples/rak/hw_test"

if [[ ! -f "${MARKER}" && ! -d "${DST_BOARD}" && ! -d "${DST_SAMPLE}" ]]; then
	echo "Nothing to uninstall under ${ZEPHYR_BASE}"
	exit 0
fi

run() {
	if [[ "${DRY_RUN}" -eq 1 ]]; then
		echo "DRY-RUN: $*"
	else
		"$@"
	fi
}

echo "Uninstalling RAK3162 BSP from ${ZEPHYR_BASE}"
run rm -rf "${DST_BOARD}" "${DST_SAMPLE}"
run rm -f "${MARKER}" "${MANIFEST}"

# Remove empty samples/rak if empty
if [[ -d "${ZEPHYR_BASE}/samples/rak" ]]; then
	if [[ "${DRY_RUN}" -eq 1 ]]; then
		echo "DRY-RUN: rmdir --ignore-fail-on-non-empty ${ZEPHYR_BASE}/samples/rak"
	else
		rmdir --ignore-fail-on-non-empty "${ZEPHYR_BASE}/samples/rak" 2>/dev/null || true
	fi
fi

echo "Done."
