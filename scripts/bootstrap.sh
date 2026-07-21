#!/usr/bin/env bash
# Bootstrap a Zephyr west workspace for the RAK3162 customer BSP.
#
# Fetches Zephyr v4.3.0 and modules declared by west.yml, installs Python
# dependencies, optionally installs Zephyr SDK 0.17.x, then exports CMake
# package paths.
#
# Usage (from this repository):
#   ./scripts/bootstrap.sh
#   ./scripts/bootstrap.sh --workspace /path/to/workspace
#   ./scripts/bootstrap.sh --no-sdk
#   ./scripts/bootstrap.sh --sdk-dir ~/zephyr-sdk-0.17.4
#   ./scripts/bootstrap.sh --build
#
# Resulting layout:
#   <workspace>/
#   ├── .west/
#   ├── rak3162-zephyr-bsp/    # this repo (west manifest)
#   ├── zephyr/                # Zephyr v4.3.0
#   └── modules/               # west dependencies
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BSP_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BSP_DIRNAME="$(basename "${BSP_ROOT}")"

ZEPHYR_SDK_VERSION="${ZEPHYR_SDK_VERSION:-0.17.4}"
WEST_UPDATE_DEPTH="${WEST_UPDATE_DEPTH:-1}"

WORKSPACE=""
SDK_DIR=""
INSTALL_SDK=1
DO_BUILD=0
WEST_GROUP_FILTER=""

usage() {
	cat <<EOF
Usage: $0 [options]

Options:
  --workspace DIR   West workspace root (default: parent of this BSP clone)
  --sdk-dir DIR     Existing or target Zephyr SDK install directory
  --no-sdk          Skip Zephyr SDK download/install
  --build           Build samples/hw_test after bootstrap
  --full            Fetch all west projects (disable narrow clone filter)
  -h, --help        Show this help

Environment:
  ZEPHYR_SDK_VERSION   SDK version to download (default: ${ZEPHYR_SDK_VERSION})
  WEST_UPDATE_DEPTH        git clone depth for west update (default: ${WEST_UPDATE_DEPTH})
EOF
}

log() { printf '==> %s\n' "$*"; }
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

have_cmd() { command -v "$1" >/dev/null 2>&1; }

detect_os_arch() {
	local os arch
	os="$(uname -s)"
	arch="$(uname -m)"
	case "${os}" in
		Linux) OS_NAME=linux ;;
		Darwin) OS_NAME=macos ;;
		*) die "Unsupported OS: ${os}" ;;
	esac
	case "${arch}" in
		x86_64|amd64) ARCH_NAME=x86_64 ;;
		aarch64|arm64) ARCH_NAME=aarch64 ;;
		*) die "Unsupported CPU architecture: ${arch}" ;;
	esac
}

ensure_host_tools() {
	local missing=()
	local c
	for c in git python3 cmake ninja wget curl tar; do
		if ! have_cmd "${c}"; then
			missing+=("${c}")
		fi
	done
	if [[ ${#missing[@]} -gt 0 ]]; then
		die "Missing host tools: ${missing[*]}. Install them and re-run."
	fi

	local py_major py_minor
	py_major="$(python3 -c 'import sys; print(sys.version_info[0])')"
	py_minor="$(python3 -c 'import sys; print(sys.version_info[1])')"
	if [[ "${py_major}" -lt 3 ]] || { [[ "${py_major}" -eq 3 ]] && [[ "${py_minor}" -lt 10 ]]; }; then
		die "Python 3.10+ required (found $(python3 --version 2>&1))"
	fi
}

ensure_west() {
	if have_cmd west; then
		log "west found: $(west --version 2>/dev/null || true)"
		return
	fi

	log "Installing west via pip"
	python3 -m pip install --user -U west
	export PATH="${HOME}/.local/bin:${PATH}"
	have_cmd west || die "west installed but not on PATH; add ~/.local/bin to PATH"
}

resolve_workspace() {
	if [[ -n "${WORKSPACE}" ]]; then
		mkdir -p "${WORKSPACE}"
		WORKSPACE="$(cd "${WORKSPACE}" && pwd)"
		return
	fi

	# Default: parent of BSP clone so west.yml self.path = rak3162-zephyr-bsp
	WORKSPACE="$(cd "${BSP_ROOT}/.." && pwd)"

	if [[ "${BSP_DIRNAME}" != "rak3162-zephyr-bsp" ]]; then
		die "This repository must live in a folder named 'rak3162-zephyr-bsp' (west.yml self.path). Found: ${BSP_DIRNAME}"
	fi
}

init_west_workspace() {
	cd "${WORKSPACE}"

	if [[ -d "${WORKSPACE}/.west" ]]; then
		log "Existing west workspace: ${WORKSPACE}"
		return
	fi

	log "Initializing west workspace at ${WORKSPACE}"
	west init -l "${BSP_ROOT}"
}

update_west_projects() {
	cd "${WORKSPACE}"

	local -a update_args=()
	if [[ -n "${WEST_UPDATE_DEPTH}" ]] && [[ "${WEST_UPDATE_DEPTH}" != "0" ]]; then
		update_args+=(-o=--depth="${WEST_UPDATE_DEPTH}")
	fi
	# Narrow fetch: skip unused HALs / optional projects for faster first sync.
	if [[ -z "${WEST_GROUP_FILTER}" ]]; then
		update_args+=(-n)
	fi

	log "Fetching Zephyr and modules (this may take a while)"
	west update "${update_args[@]}"
	west zephyr-export
}

install_python_deps() {
	local req="${WORKSPACE}/zephyr/scripts/requirements.txt"
	if [[ ! -f "${req}" ]]; then
		die "Zephyr requirements not found: ${req} (west update incomplete?)"
	fi

	log "Installing Zephyr Python dependencies"
	python3 -m pip install --user -U -r "${req}"
}

sdk_toolchain_ok() {
	local dir="$1"
	[[ -x "${dir}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]]
}

install_sdk_if_needed() {
	if [[ "${INSTALL_SDK}" -ne 1 ]]; then
		log "Skipping Zephyr SDK (--no-sdk)"
		return
	fi

	detect_os_arch

	if [[ -z "${SDK_DIR}" ]]; then
		SDK_DIR="${HOME}/zephyr-sdk-${ZEPHYR_SDK_VERSION}"
	fi

	if sdk_toolchain_ok "${SDK_DIR}"; then
		log "Zephyr SDK already present: ${SDK_DIR}"
		export ZEPHYR_SDK_INSTALL_DIR="${SDK_DIR}"
		return
	fi

	local archive="zephyr-sdk-${ZEPHYR_SDK_VERSION}_${OS_NAME}-${ARCH_NAME}_minimal.tar.xz"
	local url="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/${archive}"
	local tmpdir download

	tmpdir="$(mktemp -d)"
	download="${tmpdir}/${archive}"

	log "Downloading Zephyr SDK ${ZEPHYR_SDK_VERSION} (${archive})"
	if have_cmd wget; then
		wget -O "${download}" "${url}"
	else
		curl -fL -o "${download}" "${url}"
	fi

	log "Extracting SDK to ${SDK_DIR}"
	mkdir -p "$(dirname "${SDK_DIR}")"
	rm -rf "${SDK_DIR}"
	tar -C "$(dirname "${SDK_DIR}")" -xf "${download}"
	rm -rf "${tmpdir}"

	# Minimal archive extracts as zephyr-sdk-<ver>
	if [[ ! -d "${SDK_DIR}" ]]; then
		die "SDK extract failed; expected directory ${SDK_DIR}"
	fi

	log "Running SDK setup (arm toolchain)"
	(
		cd "${SDK_DIR}"
		./setup.sh -t arm-zephyr-eabi -c -h
	)

	sdk_toolchain_ok "${SDK_DIR}" || die "SDK install incomplete: ${SDK_DIR}"
	export ZEPHYR_SDK_INSTALL_DIR="${SDK_DIR}"
	log "Zephyr SDK ready: ${SDK_DIR}"
}

maybe_build() {
	if [[ "${DO_BUILD}" -ne 1 ]]; then
		return
	fi

	cd "${WORKSPACE}"
	# shellcheck disable=SC1091
	source "${WORKSPACE}/zephyr/zephyr-env.sh"
	if [[ -n "${ZEPHYR_SDK_INSTALL_DIR:-}" ]]; then
		export ZEPHYR_SDK_INSTALL_DIR
	fi

	log "Building samples/hw_test"
	west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/hw_test \
		--no-sysbuild --pristine always
}

print_next_steps() {
	cat <<EOF

Bootstrap complete.

Workspace: ${WORKSPACE}
BSP:       ${BSP_ROOT}
SDK:       ${ZEPHYR_SDK_INSTALL_DIR:-"(not set; use --sdk-dir or install manually)"}

Build:
  cd ${WORKSPACE}
  source zephyr/zephyr-env.sh
  export ZEPHYR_SDK_INSTALL_DIR=${ZEPHYR_SDK_INSTALL_DIR:-~/zephyr-sdk-${ZEPHYR_SDK_VERSION}}
  west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/hw_test --no-sysbuild --pristine always
  west flash

AT console: 115200 8N1
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--workspace)
			[[ $# -ge 2 ]] || die "--workspace requires a path"
			WORKSPACE="$2"
			shift 2
			;;
		--sdk-dir)
			[[ $# -ge 2 ]] || die "--sdk-dir requires a path"
			SDK_DIR="$2"
			shift 2
			;;
		--no-sdk) INSTALL_SDK=0; shift ;;
		--build) DO_BUILD=1; shift ;;
		--full) WEST_GROUP_FILTER=full; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "Unexpected argument: $1 (see --help)" ;;
	esac
done

ensure_host_tools
ensure_west
resolve_workspace
init_west_workspace
update_west_projects
install_python_deps
install_sdk_if_needed
maybe_build
print_next_steps
