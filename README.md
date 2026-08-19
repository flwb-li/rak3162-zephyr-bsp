# RAK3162 Zephyr BSP

Board support package for **RAK3162** (nRF54L15 + onboard **SX1262**).
Based on **Zephyr v4.3.0** and West.

**Repository:** https://github.com/flwb-li/rak3162-zephyr-bsp

**LoRaWAN stack:** Zephyr `CONFIG_LORAWAN` + Semtech **LoRaMac-node** (west module
`loramac-node`) — **not** Semtech BasicModem. Spec support is LoRaWAN L2
**1.0.4 / 1.1.0** (MAC advertises 1.1.x and falls back to 1.0.4 with a 1.0.x NS).
This product firmware uses **OTAA Class A**.

Product / field AT firmware: OTAA Class A, auto-join, configurable counter uplinks
(`AT+SENDINT`, default **10 s**, `0` = off), **System ON idle** between RF windows
(`AT+SLEEP` = System OFF). SX1262 is in the board DTS — **no `--shield`**.

| Doc | Path |
|-----|------|
| AT commands | `samples/at_firmware/doc/AT_COMMANDS.md` |
| Low power | `samples/at_firmware/doc/LOW_POWER.md` |
| Docker | `docker/README.md` |

Mode: **`AT+NWM`** (`0`=P2P, `1`=LoRaWAN). Region: **`AT+BAND`** (default EU868).

## Contents

| Path | Description |
|------|-------------|
| `boards/rak3162/` | Board support (DTS includes onboard SX1262) |
| `lib/rak3162_runtime/` | Board adapters: NVS, PM, LED, Sense, radio bind |
| `modules/rak-fw/` | Vendored AT + LoRaWAN/P2P framework (`CONFIG_RAK_FW`) |
| `samples/at_firmware/` | Product AT app (policy, `AT+SENDINT`, OTAA seed) |
| `docker/` | Docker image for Zephyr SDK + build tools (**recommended**) |
| `west.yml` | Manifest: Zephyr v4.3.0 + minimal modules (cmsis, hal_nordic, loramac-node) |

## Requirements

- **Docker** (recommended) **or** a west workspace with Zephyr v4.3.0 + SDK **0.17.4**
- **SWD** probe on the **host** (J-Link / CMSIS-DAP / pyOCD). No USB / serial DFU in this tree.

### Windows note

Docker works on Windows via **Docker Desktop + WSL2** (the image is Linux/`linux-x86_64`).

- Prefer keeping the west workspace under the **WSL filesystem** (e.g. `~/workspace`), not
  under `/mnt/c/...` — bind mounts from NTFS are slower and more failure-prone.
- Run `docker compose` / `west` from a **WSL** shell (or Docker Desktop integrated with that distro).
- Keep `docker/entrypoint.sh` as **LF** line endings (`core.autocrlf` / `.gitattributes` matter).
- Flash SWD tools on the **Windows host**; the container does not provide USB flashing.
- **No WSL2** (e.g. some Windows Home editions): skip Docker and use **Mode 2** with a
  native host toolchain (see below).

Details: [`docker/README.md`](docker/README.md).

## Mode 1 — Docker (recommended)

Workspace layout after `west init -l` / `west update`:

```
workspace/                      # west workspace root (.west lives here)
├── rak3162-zephyr-bsp/         # this repo (includes docker/ + modules/rak-fw)
├── zephyr/                     # Zephyr v4.3.0
└── modules/...                 # from west.yml
```

```bash
# from workspace root (Linux or WSL)
docker compose -f rak3162-zephyr-bsp/docker/compose.yaml build
docker compose -f rak3162-zephyr-bsp/docker/compose.yaml run --rm build \
  west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware -d build
```

Hex/ELF: `build/zephyr/zephyr.hex` (flash from the host).  
Details: [`docker/README.md`](docker/README.md).

## Mode 2 — West workspace

Requires a host Zephyr toolchain (see **Install toolchain without Docker** below if
you do not use Mode 1).

```bash
mkdir -p workspace && cd workspace
git clone https://github.com/flwb-li/rak3162-zephyr-bsp.git
west init -l rak3162-zephyr-bsp
west update
west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware -d build
```

`samples/at_firmware/CMakeLists.txt` injects `modules/rak-fw` via `ZEPHYR_EXTRA_MODULES`.
For other apps that need the framework:

```bash
# Linux / WSL / Git Bash
export ZEPHYR_EXTRA_MODULES="/path/to/rak3162-zephyr-bsp/modules/rak-fw"
```

```powershell
# PowerShell
$env:ZEPHYR_EXTRA_MODULES = "C:\path\to\rak3162-zephyr-bsp\modules\rak-fw"
```

### Install toolchain without Docker

For hosts without Docker/WSL2 (e.g. Windows Home), or when you prefer a native
install. Target: Zephyr **v4.3.0** + SDK **0.17.4** (`arm-zephyr-eabi` for nRF54L15).

Official reference: [Zephyr Getting Started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

#### Windows native (no WSL)

Use **PowerShell**. Prefer a short workspace path (e.g. `C:\zephyr-ws`) to reduce
path-length issues.

**Hard requirements / pitfalls (read first)**

| Do | Avoid |
|----|--------|
| **Python 3.12** for the workspace `.venv` | Python **3.14+** (`hidapi` / `west packages pip --install` often fails) |
| Plain `west update` | `west update -o=--depth=1` (breaks SHA-pinned modules such as `hal_nordic`) |
| `python -m pip install -U pip west` | Upgrading pip via bare `pip.exe` on Windows |
| Recreate `.venv` after moving/renaming the workspace | Copying or renaming an existing `.venv` (launchers keep absolute paths) |
| Manual Zephyr SDK download when GitHub is flaky | Relying only on `west sdk install` (API/`IncompleteRead` failures are common) |
| Set both SDK env vars if a clean configure fails | Leaving `ZEPHYR_TOOLCHAIN_VARIANT` empty (CMake `STREQUAL` / `FindZephyr-sdk` error) |

Confirm the west root with `west topdir`. The sample path is **relative to that
directory** (nested layout → `rak3162-zephyr-bsp/samples/at_firmware`).

**1) Host tools**

1. Install [Python 3.12](https://www.python.org/downloads/) — enable **Add python.exe to PATH**.
   Multiple Pythons can coexist; create the venv with `py -3.12 -m venv .venv`.
2. Install [Git for Windows](https://git-scm.com/download/win).
3. Install CMake (≥ 3.28), Ninja, gperf, DTC, 7-Zip via
   [Chocolatey](https://chocolatey.org/install) (**Admin** PowerShell).

   If `choco` is not found, install Chocolatey first (official one-liner):

```powershell
# Admin PowerShell — install Chocolatey
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = `
  [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString(
  'https://community.chocolatey.org/install.ps1'))
```

   Close the window, open a **new Admin** PowerShell, then:

```powershell
choco -v   # should print a version (e.g. 2.x)
choco install cmake ninja gperf dtc-msys2 7zip wget -y
```

   Official guide: https://chocolatey.org/install  
   Skip the install script if `choco -v` already works (Chocolatey is already present).

4. Open a **new** PowerShell and check:

```powershell
py -3.12 --version
git --version
cmake --version
ninja --version
dtc --version
```

5. Enable Win32 long paths (Admin PowerShell; reboot if builds still fail with
   path errors), **and** enable long paths in Git:

```powershell
New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" `
  -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force
git config --global core.longpaths true
```

**2) Python venv + west**

If `.\.venv\Scripts\Activate.ps1` is blocked, allow scripts for the current user once:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

```powershell
mkdir C:\zephyr-ws
cd C:\zephyr-ws
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -U pip west
```

Activate `.venv` in **every new** terminal before running `west` / `pip`
(`C:\zephyr-ws\.venv\Scripts\Activate.ps1`).

**3) Fetch workspace + Python deps**

```powershell
# C:\zephyr-ws , .venv active
git clone https://github.com/flwb-li/rak3162-zephyr-bsp.git
west init -l rak3162-zephyr-bsp
west update
west packages pip --install
west zephyr-export
```

`west update` can take a long time. If a fetch fails, re-run `west update` (**without**
`--depth`). Exclude the workspace from real-time antivirus scanning if files are
locked mid-clone.

If `west packages pip --install` fails on optional packages (e.g. `hidapi` /
`spsdk`), a build-only fallback is:

```powershell
python -m pip install -r zephyr\scripts\requirements-base.txt
west zephyr-export
```

**4) Install Zephyr SDK 0.17.4 (manual recommended)**

`west sdk install` downloads the **Windows** archive when run from PowerShell, and
defaults to the version in `zephyr/SDK_VERSION` (**0.17.4** for this BSP). Prefer
a manual install if GitHub access is unreliable:

```powershell
$ver = "0.17.4"
$zip = "$env:TEMP\zephyr-sdk-${ver}_windows-x86_64.7z"
$url = "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$ver/zephyr-sdk-${ver}_windows-x86_64.7z"
curl.exe -L --retry 5 --retry-all-errors -C - -o $zip $url
& "C:\Program Files\7-Zip\7z.exe" x $zip "-o$env:USERPROFILE" -y
cd $env:USERPROFILE\zephyr-sdk-0.17.4
.\setup.cmd
```

Select at least **arm-zephyr-eabi**. SDK may also live under the workspace
(e.g. `C:\zephyr-ws\zephyr-sdk-0.17.4`); then point `ZEPHYR_SDK_INSTALL_DIR` there.

Optional — `west` (often fails on flaky GitHub):

```powershell
cd zephyr
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
west sdk install --version 0.17.4 -t arm-zephyr-eabi
cd ..
```

`west zephyr-export` only registers the **Zephyr** CMake package (so
`find_package(Zephyr)` works). It does **not** set `ZEPHYR_SDK_*` in the shell.

After a successful `setup.cmd`, the SDK is registered in the CMake package
registry. Together with an existing `build/` tree (CMake cache already recorded
the toolchain), a **new PowerShell** can often `west build` again **without**
re-setting SDK environment variables. That matches a typical workflow once the
first configure has succeeded.

Set the vars below if a **clean** configure fails (`FindZephyr-sdk` /
`STREQUAL`, or “SDK not found”)—for example after `west build -p always`, a new
`-d` directory, or SDK moved:

```powershell
$env:ZEPHYR_SDK_INSTALL_DIR = "$env:USERPROFILE\zephyr-sdk-0.17.4"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
```

Optional — persist for the user account (new terminals inherit them):

```powershell
[System.Environment]::SetEnvironmentVariable(
  "ZEPHYR_SDK_INSTALL_DIR", "$env:USERPROFILE\zephyr-sdk-0.17.4", "User")
[System.Environment]::SetEnvironmentVariable(
  "ZEPHYR_TOOLCHAIN_VARIANT", "zephyr", "User")
```

**5) Build**

```powershell
# C:\zephyr-ws , .venv active, SDK env vars set
west topdir   # should print C:\zephyr-ws (or your workspace root)
west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware -d build
```

Output: `C:\zephyr-ws\build\zephyr\zephyr.hex`.

**Windows troubleshooting**

| Symptom | What to try |
|---------|-------------|
| `west` / `pip` not found | Activate `.venv` again |
| `Activate.ps1` cannot be loaded | `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` |
| `dtc` / `ninja` not found | Re-open PowerShell after Chocolatey; confirm PATH |
| `Filename longer than 260 characters` | Long-paths registry + `git config --global core.longpaths true`; short path |
| `update failed for project hal_nordic` after `--depth=1` | Delete `modules\hal\nordic`, run plain `west update` |
| `Failed building wheel for hidapi` / Python 3.14 | Recreate `.venv` with **Python 3.12** |
| `Unable to create process` / pip points at old path | Do not move `.venv`; delete and `py -3.12 -m venv .venv` |
| `west sdk install` → `IncompleteRead` / GitHub errors | Manual `.7z` download + `setup.cmd` (step 4) |
| `FindZephyr-sdk` / `STREQUAL` CMake error | Set **both** SDK env vars; `VARIANT` must be exactly `zephyr` |
| CMake cannot find Zephyr SDK | Fix `ZEPHYR_SDK_INSTALL_DIR`; re-run `setup.cmd` |
| `source directory samples/at_firmware does not exist` | Use path relative to `west topdir` (usually `rak3162-zephyr-bsp/samples/...`) |
| `west update` network / lock errors | Retry; pause antivirus on the workspace |
| Wrong Python / pip packages | Use only the workspace `.venv`, not system pip |

#### Linux (Debian/Ubuntu example)

```bash
sudo apt update
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache device-tree-compiler wget python3-dev python3-venv xz-utils file make

mkdir -p ~/workspace && cd ~/workspace
python3 -m venv .venv
source .venv/bin/activate
pip install -U pip west

git clone https://github.com/flwb-li/rak3162-zephyr-bsp.git
west init -l rak3162-zephyr-bsp
west update
west packages pip --install
west zephyr-export

cd zephyr
west sdk install --version 0.17.4 -t arm-zephyr-eabi
cd ..

west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware -d build
```

Manual SDK: extract `zephyr-sdk-0.17.4_linux-x86_64.tar.xz` under `$HOME`, then
`./setup.sh`.

## Defaults / first boot

| Item | Behavior |
|------|----------|
| OTAA seed | Temporary test keys in `samples/at_firmware/src/main.c` (`APP_OTAA_*_HEX`); seeded only when NVS has no credentials |
| Change keys | `AT+DEVEUI` / `AT+APPEUI` / `AT+APPKEY` (persisted) |
| Auto uplink | `AT+SENDINT` (default `10`; `0` disables) — Port 2, 4-byte BE FCnt, unconfirmed |
| ADR | ON by default |
| Boot print | None (`CONFIG_LOG=n`); send `AT` after reset to confirm liveness |

## Flash / console

Flash over **SWD** from the **host** (Docker containers do not expose USB SWD).
After a successful `west build` in the same workspace, prefer **`west flash`**.

Default board runner is **J-Link** (`boards/rak3162/board.cmake`:
`BOARD_FLASH_RUNNER jlink`, device `nRF54L15_M33`).

```bash
# workspace root, same build dir as west build (-d build)
west flash -d build
```

```powershell
# Windows native — J-Link software on PATH; probe connected
west flash -d build
```

Other runners supported by the board file:

```bash
west flash -d build --runner nrfutil
west flash -d build --runner pyocd
west flash -d build --runner nrfjprog
```

Requirements:

| Runner | Host tool |
|--------|-----------|
| `jlink` (default) | [SEGGER J-Link](https://www.segger.com/downloads/jlink/) |
| `nrfutil` | [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util) |
| `pyocd` | `pip install pyocd` (in the workspace `.venv`) |
| `nrfjprog` | nRF Command Line Tools |

Manual fallback (without west):

```text
nrfutil device program --firmware build/zephyr/zephyr.hex
```

AT UART: TX=P1.06, RX=P1.07, **115200 8N1** (PuTTY, Tera Term, etc.).

See `samples/at_firmware/doc/AT_COMMANDS.md` for the full command set.
