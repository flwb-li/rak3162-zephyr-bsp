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
| Docker (optional) | `docker/README.md` |

Mode: **`AT+NWM`** (`0`=P2P, `1`=LoRaWAN). Region: **`AT+BAND`** (default EU868).

## Contents

| Path | Description |
|------|-------------|
| `boards/rak3162/` | Board support (DTS includes onboard SX1262) |
| `lib/rak3162_runtime/` | Board adapters: NVS, PM, LED, Sense, radio bind |
| `modules/rak-fw/` | Vendored AT + LoRaWAN/P2P framework (`CONFIG_RAK_FW`) |
| `samples/at_firmware/` | Product AT app (policy, `AT+SENDINT`, OTAA seed) |
| `docker/` | Optional Docker image for Zephyr SDK + build tools |
| `west.yml` | Manifest: Zephyr v4.3.0 + minimal modules (cmsis, hal_nordic, loramac-node) |

## Requirements

- **VS Code** with [Workbench for Zephyr](https://marketplace.visualstudio.com/items?itemName=ac6.zephyr-workbench)
  **or** [IDE for Zephyr](https://marketplace.visualstudio.com/items?itemName=mylonics.zephyr-ide) (**recommended**)
- Alternatively: a west CLI workspace, or **Docker**, with Zephyr **v4.3.0** + SDK **0.17.4**
- **SWD** probe on the **host** (J-Link / CMSIS-DAP / pyOCD). No USB / serial DFU in this tree.

### Windows note

The VS Code extensions run **natively** on Windows (WSL2 is not required). They can
install host tools and Zephyr SDK **0.17.4**.

Docker is optional and needs **Docker Desktop + WSL2** (the image is Linux/`linux-x86_64`):

- Prefer keeping the west workspace under the **WSL filesystem** (e.g. `~/workspace`), not
  under `/mnt/c/...` — bind mounts from NTFS are slower and more failure-prone.
- Run `docker compose` / `west` from a **WSL** shell (or Docker Desktop integrated with that distro).
- Keep `docker/entrypoint.sh` as **LF** line endings (`core.autocrlf` / `.gitattributes` matter).
- Flash SWD tools on the **Windows host**; the container does not provide USB flashing.

Details: [`docker/README.md`](docker/README.md).

## Mode 1 — VS Code (recommended)

Use **this repository as the west manifest**. Do **not** start from a generic Zephyr
“Minimal / Full” vendor template: that workspace will not include this BSP’s board,
`rak-fw`, or the pinned modules (`hal_nordic`, `loramac-node`).

Workspace after import / `west update`:

```
workspace/                      # west workspace root (.west lives here)
├── rak3162-zephyr-bsp/         # this repo (manifest; includes modules/rak-fw)
├── zephyr/                     # Zephyr v4.3.0
└── modules/...                 # from west.yml
```

| Item | Value |
|------|--------|
| Manifest | this repo (`west.yml`) |
| Zephyr | **v4.3.0** (from the manifest) |
| SDK | **0.17.4**, toolchain `arm-zephyr-eabi` |
| Board | `rak3162/nrf54l15/cpuapp` |
| Application | `rak3162-zephyr-bsp/samples/at_firmware` |

Build and flash from the IDE status bar, or use `west flash` (see **Flash / console**).
Default runner is **J-Link**.

`samples/at_firmware/CMakeLists.txt` injects `modules/rak-fw` via `ZEPHYR_EXTRA_MODULES`.
For other apps that need the framework, set `ZEPHYR_EXTRA_MODULES` as in **Mode 2**.

### Workbench for Zephyr (Ac6)

Marketplace: [Workbench for Zephyr](https://marketplace.visualstudio.com/items?itemName=ac6.zephyr-workbench)
(publisher **Ac6**).  
Guides: [Zephyr docs](https://docs.zephyrproject.org/latest/develop/tools/workbench_for_zephyr.html),
[install](https://zephyr-workbench.com/docs/documentation/installation/),
[west workspace](https://zephyr-workbench.com/docs/documentation/west-workspace/),
[applications](https://zephyr-workbench.com/docs/documentation/application).

**1) Install the extension**

1. Install [VS Code](https://code.visualstudio.com/).
2. Extensions → search **Workbench for Zephyr** → **Install**. Companion extensions
   (C/C++, Serial Monitor, Cortex-Debug, Devicetree) install automatically.
3. Click the **Workbench for Zephyr** icon in the Activity Bar (left).

**2) Install Host Tools**

1. In the Workbench view, click **Install Host Tools**.
2. Wait a few minutes. Windows will prompt for UAC; Linux may ask for `sudo`;
   macOS needs [Homebrew](https://brew.sh/) first.
3. Portable tools land under `~/.zinstaller` (Windows: `%USERPROFILE%\.zinstaller`).

To keep an existing Python/CMake, use **Install Host Tools (Advanced)** instead.

**3) Add the Zephyr SDK (toolchain)**

1. Click **Add Toolchain** / **Import Toolchain**.
2. Toolchain family: **Zephyr SDK**. Source: **Official**.
3. SDK Type: **Minimal**. Architecture: **arm** (`arm-zephyr-eabi`).
4. Version: **0.17.4** (do not take “latest” if it is newer than this BSP).
5. Destination: **Global (auto-discovered)** or a folder you pick → **Import**.

**4) Add the west workspace (this BSP, not a vendor template)**

Click **Add West Workspace**. **Do not** use **From template** (STM32 / Nordic / …).

Pick one source:

| Source | When to use | What to enter |
|--------|-------------|---------------|
| **Repository** | First-time clone | Path: `https://github.com/flwb-li/rak3162-zephyr-bsp.git`. Revision: `main`. Manifest: leave empty (`west.yml`). Location: parent folder (e.g. `C:\zephyr-ws` or `~/zephyr-ws`). Subfolder: workspace name. |
| **Local manifest** | You already cloned this repo | Browse to `rak3162-zephyr-bsp/west.yml`. Location: the **parent** of that clone (west root). |
| **Local folder** | You already ran `west init` / `west update` | Select the folder that contains `.west`. |

Recommended extras:

- Enable **Create a dedicated Python venv** for the workspace.
- Leave **Fetch west blobs** checked (`hal_nordic` firmware).

Click **Import** and wait (often ~10 minutes). When it finishes you should see
`rak3162-zephyr-bsp/`, `zephyr/`, and `modules/` as siblings.

Windows `PermissionError: [WinError 5]`: close VS Code, init from a terminal
(`git clone` + `west init -l rak3162-zephyr-bsp` + `west update`), then import
with **Local folder**.

**5) Install the J-Link runner**

This board’s default flash runner is **J-Link** (not OpenOCD).

1. Click **Install Runners**.
2. Install **J-Link** (or add an existing SEGGER install under Extra Runners).
3. Connect the SWD probe to the RAK3162.

**6) Import `at_firmware` (do not copy `hello_world`)**

1. Click **Add Application** (or `Zephyr Workbench: Add Application`).
2. West Workspace: the workspace from step 4.
3. Toolchain: the SDK from step 3. If the status line warns about a Zephyr/SDK
   mismatch, pick **0.17.4**.
4. Board: **RAK3162** / `rak3162/nrf54l15/cpuapp`. If it is missing from the
   list, choose **Enter custom board…** and type `rak3162/nrf54l15/cpuapp`.
5. **New or existing application?** → **Import existing application**.
6. Project Location → Browse to
   `<west-root>/rak3162-zephyr-bsp/samples/at_firmware`
   (folder with `prj.conf` and `CMakeLists.txt`).
7. Click **Create**. Do **not** use **Create new application** + `hello_world`.

Right-click the application → **Set Default Flash Runner** → **jlink**
(or `nrfutil` / `pyocd` if that is your probe).

If configure fails on sysbuild, right-click → **Sysbuild → Disable**, or add
`--no-sysbuild` under **Arguments & Environment → west Arguments**.

**7) Build, flash, serial**

1. In **Applications**, click the gear / **Build**, or use the status-bar **Build**
   when a file from the app is open. Output is in the Terminal.
2. Hex: `samples/at_firmware/build/zephyr/zephyr.hex` (or the build folder the
   wizard created).
3. Hover the application → **Flash**, or right-click → **Flash/Run**.
4. **Serial Monitor** tab: 115200 8N1, UART0 TX=P1.06 RX=P1.07. Reset the board;
   you should see `RAK3162 AT firmware ready` then the `AT?` list and `OK`.

**Workbench troubleshooting**

| Symptom | What to try |
|---------|-------------|
| Board `rak3162/...` not listed | Workspace import not finished, or you used a vendor template. Re-import this repo’s `west.yml`. Use **Enter custom board…**. |
| `hello_world` builds but AT firmware is missing | You created a new sample. **Import existing application** on `samples/at_firmware`. |
| Flash wants OpenOCD | **Set Default Flash Runner** → `jlink`; install J-Link from **Install Runners**. |
| `PermissionError` / Access denied on Windows | Close VS Code; `west init -l` + `west update` in a terminal; **Local folder**. |
| SDK / Zephyr version warning | Toolchain must be **0.17.4**; workspace Zephyr comes from this manifest (**v4.3.0**). |

### IDE for Zephyr (Mylonics)

Marketplace: [IDE for Zephyr](https://marketplace.visualstudio.com/items?itemName=mylonics.zephyr-ide)
(publisher **Mylonics**; formerly Zephyr IDE), or the
[extension pack](https://marketplace.visualstudio.com/items?itemName=mylonics.zephyr-ide-extension-pack)
(adds C/C++, Cortex-Debug, Serial Monitor, Devicetree).  
Guide: [IDE for Zephyr](https://zephyr-ide.mylonics.com/)
([install](https://zephyr-ide.mylonics.com/getting-started/installation/),
[setup panel](https://zephyr-ide.mylonics.com/getting-started/setup-panel/),
[workspace](https://zephyr-ide.mylonics.com/getting-started/workspace-configuration/),
[SDK](https://zephyr-ide.mylonics.com/getting-started/sdk-installation/),
[projects](https://zephyr-ide.mylonics.com/user-guide/project-setup/)).

**1) Install the extension and open a folder**

1. Install [VS Code](https://code.visualstudio.com/) and **IDE for Zephyr**.
2. Create an empty west-root folder (short path on Windows, e.g. `C:\zephyr-ws`).
3. **File → Open Folder** on that directory (this becomes the west workspace root).
4. Click the **IDE for Zephyr** / **Zephyr** icon in the Activity Bar, or run
   `Zephyr IDE: Workspace Setup` from the Command Palette (`Ctrl+Shift+P` /
   `Cmd+Shift+P`).

The Setup Panel has three cards: **Host Tools**, **Zephyr SDK Management**,
**Workspace**. Do them in that order (SDK needs a west workspace).

**2) Host Tools**

1. Open the **Host Tools** card. Review detected vs missing tools (CMake, Python 3,
   Ninja, DTC, gcc).
2. Click **Install Host Tools**. Windows uses `winget`; Linux uses the distro
   package manager (`sudo`); macOS uses Homebrew.
3. Re-run **Check Build Dependencies** if a tool stays red.

Skip this card if the tools are already on PATH.

**3) Workspace from this BSP (not a Standard / vendor template)**

Open the **Workspace** card. **Do not** use **Standard Workspace** or
**Create new west.yml** (those pull a generic Zephyr tree without this board).

Pick one:

| Method | When to use | What to enter |
|--------|-------------|---------------|
| **West Workspace from Git** | Empty folder, first clone | Repo URL `https://github.com/flwb-li/rak3162-zephyr-bsp.git`. Use this repo’s `west.yml`. |
| **Open Current Directory → Use west.yml file** | You already cloned the BSP into the open folder | Point at `rak3162-zephyr-bsp/west.yml` (same as `west init -l`). |
| **Open Current Directory → Use .west folder** | Workspace already initialized | Keep the existing `.west`. |
| **Setup Workspace from External Directory** | West root is not the folder VS Code opened | Browse to the folder that contains (or will contain) `.west`. |

The wizard then: creates `.venv` + west → `west init` → `west update` → pip
packages. Wait until the Workspace card shows the install as ready. You should
have `rak3162-zephyr-bsp/`, `zephyr/` (v4.3.0), and `modules/` under the west root.

If `west update` fails, run `Zephyr IDE: Re-run West Setup`, or `west update` in
the IDE’s west terminal (**without** `--depth=1`).

**4) Zephyr SDK 0.17.4**

1. Open **Zephyr SDK Management** (only after the workspace exists).
2. Version: **0.17.4** (the panel may offer “latest”; this BSP needs 0.17.4).
3. Architecture / toolchain: **arm-zephyr-eabi** (you do not need every arch).
4. **Install SDK**. This runs `west sdk` and can take several minutes.

Optional: in `.vscode/zephyr-ide.json` set `"sdkVersion": "0.17.4"` and
`"toolchains": ["arm-zephyr-eabi"]`, then **Install from zephyr-ide.json**.

**5) Add the AT firmware project and a build**

1. In the project tree: **Add Project** (`Zephyr IDE: Add Project`) — add an
   **existing** app, do **not** **Create Project From Template** (`blinky` /
   `hello_world`).
2. Select `<west-root>/rak3162-zephyr-bsp/samples/at_firmware`.
3. **Add Build Configuration** (`Zephyr IDE: Add Build Configuration`):
   - Board: `rak3162/nrf54l15/cpuapp`
   - Optimization: as you prefer (debug vs size)
4. Bind a **Runner Profile** for flash: **jlink** (install
   [SEGGER J-Link](https://www.segger.com/downloads/jlink/) on the host).
   CMSIS-DAP: `pyocd` (the extension can install `pyocd`). Nordic USB: `nrfutil`.

If the first configure fails on sysbuild, add `--no-sysbuild` to that build’s
west args.

**6) Build, flash, serial**

1. Status bar or project panel: **Build** (`Zephyr IDE: Build`). Pristine rebuild:
   **Build Pristine**.
2. Hex is under the build config directory (see `.vscode/zephyr-ide.json`),
   typically `.../zephyr/zephyr.hex`.
3. **Flash** or **Build and Flash** with the probe connected.
4. Serial Monitor: 115200 8N1, UART0 TX=P1.06 RX=P1.07. Reset; expect
   `RAK3162 AT firmware ready`, the command list, then `OK`.

**IDE for Zephyr troubleshooting**

| Symptom | What to try |
|---------|-------------|
| SDK card disabled / empty | Finish **Workspace** first; SDK management needs west. |
| Generic Zephyr tree, no `rak3162` board | You used Standard / vendor `west.yml`. Reset workspace and import **this** `west.yml`. |
| `Add Project` only offers Zephyr samples | Use **Add Project** (existing folder), not **Create Project From Template**. |
| Python / `hidapi` errors on Windows | Use Python **3.12** for the workspace venv, not 3.14+. |
| Flash runner missing | Install J-Link (or pyOCD / nRF Util) on the host; bind it on the build’s Runner Profile. |
| Board not found | Confirm `west.yml` update finished (`modules/hal/nordic` present) and board id is `rak3162/nrf54l15/cpuapp`. |

## Mode 2 — West CLI

On Windows, prefer **Mode 1** (Workbench / IDE for Zephyr install the toolchain).
The commands below assume `west` and Zephyr SDK **0.17.4** are already on PATH.

```bash
mkdir -p workspace && cd workspace
git clone https://github.com/flwb-li/rak3162-zephyr-bsp.git
west init -l rak3162-zephyr-bsp
west update
west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware -d build
```

For other apps that need `rak-fw`:

```bash
# Linux / WSL / Git Bash
export ZEPHYR_EXTRA_MODULES="/path/to/rak3162-zephyr-bsp/modules/rak-fw"
```

```powershell
# PowerShell
$env:ZEPHYR_EXTRA_MODULES = "C:\path\to\rak3162-zephyr-bsp\modules\rak-fw"
```

### Linux toolchain (Debian/Ubuntu example)

Official reference: [Zephyr Getting Started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).
Target: Zephyr **v4.3.0** + SDK **0.17.4** (`arm-zephyr-eabi`).

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

## Mode 3 — Docker (optional)

From the west workspace root (Linux or WSL), after `west init -l` / `west update`:

```bash
docker compose -f rak3162-zephyr-bsp/docker/compose.yaml build
docker compose -f rak3162-zephyr-bsp/docker/compose.yaml run --rm build \
  west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware -d build
```

Hex/ELF: `build/zephyr/zephyr.hex` (flash from the host).  
Details: [`docker/README.md`](docker/README.md).

## Defaults / first boot

| Item | Behavior |
|------|----------|
| OTAA seed | Temporary test keys in `samples/at_firmware/src/main.c` (`APP_OTAA_*_HEX`); seeded only when NVS has no credentials |
| Change keys | `AT+DEVEUI` / `AT+APPEUI` / `AT+APPKEY` (persisted) |
| Auto uplink | `AT+SENDINT` (default `10`; `0` disables) — Port 2, 4-byte BE FCnt, unconfirmed |
| ADR | ON by default |
| Boot print | On reset: AT command list (same as `AT?`) then `OK` |

## Flash / console

Flash over **SWD** from the **host** (Docker containers do not expose USB SWD).
After a successful IDE build or `west build` in the same workspace, prefer the
IDE **Flash** button or **`west flash`**.

Default board runner is **J-Link** (`boards/rak3162/board.cmake`:
`BOARD_FLASH_RUNNER jlink`, device `nRF54L15_M33`).

```bash
# workspace root, same build dir as west build (-d build)
west flash -d build
```

```powershell
# Windows — J-Link software on PATH; probe connected
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
