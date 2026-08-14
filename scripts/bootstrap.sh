#!/usr/bin/env bash
# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0

# Bootstrap a self-contained RAK3162 Zephyr workspace for customers.
#
# DEPRECATED: prefer in-tree Docker (docker/compose.yaml from workspace root).
# This script remains for offline/legacy hosts only.
#
# Designed for a machine with *no* prior Zephyr / west / SDK setup.
# Clears inherited Zephyr environment variables, creates a workspace-local
# Python venv, fetches Zephyr v4.3.0 + minimal modules, and installs the
# Zephyr SDK into the workspace (unless --no-sdk / --sdk-dir).
#
# Host prerequisites (only):
#   git, python3 (>=3.10), cmake, ninja, wget|curl, tar
#
# Usage:
#   mkdir my-ws && cd my-ws
#   git clone <bsp-url> rak3162-zephyr-bsp
#   ./rak3162-zephyr-bsp/scripts/bootstrap.sh
#   source ./env.sh
#   west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware --no-sysbuild
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BSP_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BSP_DIRNAME="$(basename "${BSP_ROOT}")"

ZEPHYR_SDK_VERSION="${ZEPHYR_SDK_VERSION:-0.17.4}"
# Default 0 = full fetch. Shallow (--depth=1) often fails for SHA-pinned modules
# such as hal_nordic (west fetch --tags --depth=1 does not retrieve the commit).
WEST_UPDATE_DEPTH="${WEST_UPDATE_DEPTH:-0}"
RETRY_MAX="${RETRY_MAX:-5}"
RETRY_DELAY_SEC="${RETRY_DELAY_SEC:-10}"

WORKSPACE=""
SDK_DIR=""
INSTALL_SDK=1
DO_BUILD=0
VENV_DIR=""

usage() {
	cat <<EOF
Usage: $0 [options]

Customer bootstrap from a clean machine (no Zephyr environment required).

Creates/uses a west workspace, a local Python venv (.venv), fetches Zephyr
v4.3.0 + minimal modules (cmsis, cmsis_6, hal_nordic, loramac-node), installs
Zephyr SDK ${ZEPHYR_SDK_VERSION} into the workspace, and writes env.sh.

Options:
  --workspace DIR   West workspace root (default: parent of this BSP clone)
  --sdk-dir DIR     Use/install SDK at DIR (default: <workspace>/zephyr-sdk-${ZEPHYR_SDK_VERSION})
  --no-sdk          Skip Zephyr SDK download/install
  --build           Build samples/at_firmware after bootstrap
  --retries N       Max attempts for network steps (default: ${RETRY_MAX})
  -h, --help        Show this help

Environment:
  ZEPHYR_SDK_VERSION   SDK version to download (default: ${ZEPHYR_SDK_VERSION})
  WEST_UPDATE_DEPTH        git clone depth for west update (default: 0 = full; shallow
                       --depth=1 can fail on SHA-pinned modules like hal_nordic)
  RETRY_MAX            Same as --retries (default: ${RETRY_MAX})
  RETRY_DELAY_SEC      Base delay between retries in seconds (default: ${RETRY_DELAY_SEC})
EOF
}

log() { printf '==> %s\n' "$*"; }
warn() { printf 'Warning: %s\n' "$*" >&2; }
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

have_cmd() { command -v "$1" >/dev/null 2>&1; }

# Retry a command on failure (network flakes, early EOF, etc.).
# Usage: retry <label> <cmd> [args...]
retry() {
	local label="$1"
	shift
	local attempt=1
	local delay="${RETRY_DELAY_SEC}"
	local rc=0

	while [[ "${attempt}" -le "${RETRY_MAX}" ]]; do
		log "${label} (attempt ${attempt}/${RETRY_MAX})"
		set +e
		"$@"
		rc=$?
		set -e
		if [[ "${rc}" -eq 0 ]]; then
			return 0
		fi
		if [[ "${attempt}" -ge "${RETRY_MAX}" ]]; then
			die "${label} failed after ${RETRY_MAX} attempts (exit ${rc})"
		fi
		warn "${label} failed (exit ${rc}); retrying in ${delay}s..."
		sleep "${delay}"
		# Exponential backoff, capped at 120s.
		delay=$((delay * 2))
		if [[ "${delay}" -gt 120 ]]; then
			delay=120
		fi
		attempt=$((attempt + 1))
	done
}

# Drop any pre-existing Zephyr/west environment from the caller's shell.
scrub_zephyr_env() {
	local var
	local -a drop=(
		ZEPHYR_BASE
		ZEPHYR_SDK_INSTALL_DIR
		ZEPHYR_TOOLCHAIN_VARIANT
		ZEPHYR_EXTRA_MODULES
		ZEPHYR_MODULES
		ZEPHYR_BOARD_ALIASES
		BOARD_ROOT
		DTS_ROOT
		SOC_ROOT
		ARCH_ROOT
		CMAKE_PREFIX_PATH
		WEST_WORKSPACE
	)

	for var in "${drop[@]}"; do
		if [[ -n "${!var:-}" ]]; then
			log "Clearing inherited ${var}=${!var}"
			unset "${var}" || true
		fi
	done
}

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
	for c in git python3 cmake ninja tar; do
		if ! have_cmd "${c}"; then
			missing+=("${c}")
		fi
	done
	if ! have_cmd wget && ! have_cmd curl; then
		missing+=("wget|curl")
	fi
	if [[ ${#missing[@]} -gt 0 ]]; then
		die "Missing host tools: ${missing[*]}. Install them and re-run (no Zephyr packages needed)."
	fi

	local py_major py_minor
	py_major="$(python3 -c 'import sys; print(sys.version_info[0])')"
	py_minor="$(python3 -c 'import sys; print(sys.version_info[1])')"
	if [[ "${py_major}" -lt 3 ]] || { [[ "${py_major}" -eq 3 ]] && [[ "${py_minor}" -lt 10 ]]; }; then
		die "Python 3.10+ required (found $(python3 --version 2>&1))"
	fi
}

resolve_workspace() {
	if [[ -n "${WORKSPACE}" ]]; then
		mkdir -p "${WORKSPACE}"
		WORKSPACE="$(cd "${WORKSPACE}" && pwd)"
	else
		WORKSPACE="$(cd "${BSP_ROOT}/.." && pwd)"
	fi

	if [[ "${BSP_DIRNAME}" != "rak3162-zephyr-bsp" ]]; then
		die "This repository must live in a folder named 'rak3162-zephyr-bsp' (west.yml self.path). Found: ${BSP_DIRNAME}"
	fi

	VENV_DIR="${WORKSPACE}/.venv"
}

activate_venv() {
	# shellcheck disable=SC1091
	source "${VENV_DIR}/bin/activate"
	hash -r 2>/dev/null || true
}

ensure_venv_and_west() {
	if [[ ! -d "${VENV_DIR}" ]]; then
		log "Creating workspace Python venv: ${VENV_DIR}"
		python3 -m venv "${VENV_DIR}"
	else
		log "Using existing venv: ${VENV_DIR}"
	fi

	activate_venv

	log "Installing west into workspace venv"
	retry "pip upgrade" python -m pip install -U pip
	retry "pip install west" python -m pip install -U west

	have_cmd west || die "west missing after venv install"
	log "west ready: $(west --version 2>/dev/null || true) ($(command -v west))"
}

# Remove incomplete west project checkouts so the next attempt can re-clone.
cleanup_incomplete_west_projects() {
	local proj path
	# Projects declared by this BSP's minimal allowlist (+ zephyr itself).
	for proj in zephyr modules/hal/cmsis modules/hal/cmsis_6 modules/hal/nordic modules/lib/loramac-node; do
		path="${WORKSPACE}/${proj}"
		if [[ ! -e "${path}" ]]; then
			continue
		fi

		local broken=0
		if [[ -d "${path}/.git" ]]; then
			if ! git -C "${path}" rev-parse --verify HEAD >/dev/null 2>&1; then
				broken=1
			elif [[ "${proj}" == "zephyr" ]] && [[ ! -f "${path}/CMakeLists.txt" ]]; then
				broken=1
			elif [[ "${proj}" != "zephyr" ]] && [[ ! -f "${path}/zephyr/module.yml" ]] && [[ ! -f "${path}/CMakeLists.txt" ]]; then
				broken=1
			fi
		elif [[ -d "${path}" ]]; then
			# Empty or non-git leftover directory from a failed clone.
			broken=1
		fi

		if [[ "${broken}" -eq 1 ]]; then
			warn "Removing incomplete checkout: ${path}"
			rm -rf "${path}"
		fi
	done
}

init_west_workspace() {
	cd "${WORKSPACE}"
	activate_venv
	scrub_zephyr_env

	if [[ -d "${WORKSPACE}/.west" ]]; then
		log "Existing west workspace: ${WORKSPACE}"
		return
	fi

	log "Initializing west workspace at ${WORKSPACE}"
	west init -l "${BSP_ROOT}"
}

do_west_update() {
	local -a update_args=()

	cleanup_incomplete_west_projects

	if [[ -n "${WEST_UPDATE_DEPTH}" ]] && [[ "${WEST_UPDATE_DEPTH}" != "0" ]]; then
		warn "Using shallow fetch depth=${WEST_UPDATE_DEPTH} (may fail for SHA-pinned modules)"
		update_args+=(-o=--depth="${WEST_UPDATE_DEPTH}")
	fi

	west update "${update_args[@]}"
}

update_west_projects() {
	cd "${WORKSPACE}"
	activate_venv
	scrub_zephyr_env

	log "Fetching Zephyr v4.3.0 + minimal modules (west.yml name-allowlist)"
	retry "west update" do_west_update
	west zephyr-export
	log "Projects present after update:"
	west list
}

install_python_deps() {
	local req="${WORKSPACE}/zephyr/scripts/requirements.txt"
	if [[ ! -f "${req}" ]]; then
		die "Zephyr requirements not found: ${req} (west update incomplete?)"
	fi

	activate_venv
	log "Installing Zephyr Python dependencies into venv"
	retry "pip install Zephyr requirements" python -m pip install -U -r "${req}"
}

sdk_toolchain_ok() {
	local dir="$1"
	[[ -x "${dir}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]]
}

install_sdk_if_needed() {
	if [[ "${INSTALL_SDK}" -ne 1 ]]; then
		log "Skipping Zephyr SDK download (--no-sdk)"
		if [[ -n "${SDK_DIR}" ]]; then
			export ZEPHYR_SDK_INSTALL_DIR="${SDK_DIR}"
			log "Using existing SDK: ${ZEPHYR_SDK_INSTALL_DIR}"
		elif [[ -n "${ZEPHYR_SDK_INSTALL_DIR:-}" ]]; then
			log "Using ZEPHYR_SDK_INSTALL_DIR=${ZEPHYR_SDK_INSTALL_DIR}"
		fi
		return
	fi

	detect_os_arch

	if [[ -z "${SDK_DIR}" ]]; then
		SDK_DIR="${WORKSPACE}/zephyr-sdk-${ZEPHYR_SDK_VERSION}"
	fi

	if sdk_toolchain_ok "${SDK_DIR}"; then
		log "Zephyr SDK already present: ${SDK_DIR}"
		export ZEPHYR_SDK_INSTALL_DIR="${SDK_DIR}"
		return
	fi

	local archive="zephyr-sdk-${ZEPHYR_SDK_VERSION}_${OS_NAME}-${ARCH_NAME}_minimal.tar.xz"
	local url="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/${archive}"
	local tmpdir download parent

	tmpdir="$(mktemp -d)"
	download="${tmpdir}/${archive}"
	parent="$(dirname "${SDK_DIR}")"
	mkdir -p "${parent}"

	download_sdk() {
		rm -f "${download}"
		if have_cmd wget; then
			wget -O "${download}" "${url}"
		else
			curl -fL --retry 0 -o "${download}" "${url}"
		fi
		# Basic sanity: archive must be non-trivial.
		[[ -s "${download}" ]] || return 1
		local sz
		sz="$(wc -c <"${download}" | tr -d ' ')"
		[[ "${sz}" -gt 1000000 ]] || return 1
	}

	log "Downloading Zephyr SDK ${ZEPHYR_SDK_VERSION} (${archive})"
	retry "SDK download" download_sdk

	log "Extracting SDK to ${SDK_DIR}"
	rm -rf "${SDK_DIR}"
	tar -C "${parent}" -xf "${download}"
	rm -rf "${tmpdir}"

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

write_env_script() {
	local env_file="${WORKSPACE}/env.sh"
	local sdk_export="${ZEPHYR_SDK_INSTALL_DIR:-${WORKSPACE}/zephyr-sdk-${ZEPHYR_SDK_VERSION}}"

	cat > "${env_file}" <<EOF
# Auto-generated by rak3162-zephyr-bsp/scripts/bootstrap.sh
# Usage: source ${env_file}
#
# Activates the workspace Python venv and Zephyr environment for this tree only.
_WS="\$(cd "\$(dirname "\${BASH_SOURCE[0]:-\$0}")" && pwd)"

# Ignore any Zephyr settings from other shells/workspaces.
unset ZEPHYR_BASE ZEPHYR_EXTRA_MODULES ZEPHYR_MODULES ZEPHYR_BOARD_ALIASES BOARD_ROOT DTS_ROOT SOC_ROOT ARCH_ROOT WEST_WORKSPACE 2>/dev/null || true

if [[ -f "\${_WS}/.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "\${_WS}/.venv/bin/activate"
fi

if [[ -f "\${_WS}/zephyr/zephyr-env.sh" ]]; then
	# shellcheck disable=SC1091
	source "\${_WS}/zephyr/zephyr-env.sh"
fi

export ZEPHYR_SDK_INSTALL_DIR="\${ZEPHYR_SDK_INSTALL_DIR:-${sdk_export}}"
export ZEPHYR_TOOLCHAIN_VARIANT="\${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"

printf 'RAK3162 workspace ready:\\n  ZEPHYR_BASE=%s\\n  ZEPHYR_SDK_INSTALL_DIR=%s\\n  west=%s\\n' \\
	"\${ZEPHYR_BASE:-}" "\${ZEPHYR_SDK_INSTALL_DIR:-}" "\$(command -v west 2>/dev/null || echo missing)"
EOF

	log "Wrote ${env_file}"
}

maybe_build() {
	if [[ "${DO_BUILD}" -ne 1 ]]; then
		return
	fi

	cd "${WORKSPACE}"
	activate_venv
	# shellcheck disable=SC1091
	source "${WORKSPACE}/env.sh"

	log "Building samples/at_firmware"
	west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware \
		--no-sysbuild --pristine always
}

print_next_steps() {
	cat <<EOF

Bootstrap complete (self-contained workspace, no prior Zephyr required).

Workspace: ${WORKSPACE}
BSP:       ${BSP_ROOT}
Venv:      ${VENV_DIR}
SDK:       ${ZEPHYR_SDK_INSTALL_DIR:-"(not installed; pass --sdk-dir or re-run without --no-sdk)"}

Build (always source env.sh in a new shell):
  cd ${WORKSPACE}
  source ./env.sh
  west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware --no-sysbuild --pristine always
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
		--retries)
			[[ $# -ge 2 ]] || die "--retries requires a number"
			RETRY_MAX="$2"
			shift 2
			;;
		-h|--help) usage; exit 0 ;;
		*) die "Unexpected argument: $1 (see --help)" ;;
	esac
done

if ! [[ "${RETRY_MAX}" =~ ^[1-9][0-9]*$ ]]; then
	die "--retries / RETRY_MAX must be a positive integer (got: ${RETRY_MAX})"
fi

scrub_zephyr_env
ensure_host_tools
resolve_workspace
ensure_venv_and_west
init_west_workspace
update_west_projects
install_python_deps
install_sdk_if_needed
write_env_script
maybe_build
print_next_steps
