# RAK3162 Zephyr BSP

Board support package for **RAK3162** (nRF54L15) with **onboard SX1262** LoRa radio.
Based on **Zephyr v4.3.0** and West.

**Repository:** https://github.com/flwb-li/rak3162-zephyr-bsp

This tree is for **practical use / evaluation**: build, flash, OTAA Class A uplink/downlink, and LoRa P2P.
It is **not** a drop-in RUI3 firmware replacement. AT commands are a **small practical subset** inspired by RUI3 — see `samples/hw_test/doc/AT_COMMANDS.md`.

SX1262 is part of the board device tree — **no `--shield` is required**.
LoRaWAN uses Zephyr **`CONFIG_LORAWAN`** (loramac-node). LoRa P2P uses Zephyr **`CONFIG_LORA`** (`AT+P2P` / `PRECV` / `PSEND` / `CW`). There is **no Semtech USP** dependency.
Select mode with **`AT+NWM`** (`0`=P2P, `1`=LoRaWAN). Region via **`AT+BAND`** before the first join.

## Contents

| Path | Description |
|------|-------------|
| `boards/rak3162/` | Board support (DTS includes onboard SX1262 as `semtech,sx1262`) |
| `samples/hw_test/` | Single sample: AT console + LoRa P2P + LoRaWAN OTAA Class A |
| `zephyr/module.yml` | Registers this tree as a Zephyr module (`board_root`) |
| `west.yml` | West manifest (Zephyr v4.3.0 + minimal modules) — Mode 1 |
| `scripts/bootstrap.sh` | Mode 1: fetch Zephyr/modules (+ optional SDK) for this BSP |

## Requirements

On a **clean** machine (no Zephyr / west / SDK preinstalled):

- `git`, `python3` **3.10+**, `cmake`, `ninja`, `wget` or `curl`, `tar`
- Network access to GitHub (Zephyr + SDK download)
- An **SWD debug probe** to flash the module (J-Link, CMSIS-DAP / DAPLink, or another pyOCD-compatible probe)

`./scripts/bootstrap.sh` installs west, Python deps, and Zephyr SDK into the workspace.
Flash tools (J-Link / pyOCD / nRF Util) are **not** installed by bootstrap — see [Flash](#flash).

There are **two** ways to use this BSP:

| Mode | When to use |
|------|-------------|
| **1 — Dedicated west workspace** | Clean PC / standalone RAK3162 workspace (recommended for new users) |
| **2 — External Zephyr module** | You already have Zephyr v4.3.0; keep BSP outside the tree |

## Mode 1 — Dedicated west workspace (recommended for clean PCs)

### One-shot bootstrap

No prior Zephyr environment is required. Clone this repo as folder `rak3162-zephyr-bsp`, then:

```bash
mkdir ~/rak3162-workspace && cd ~/rak3162-workspace
git clone https://github.com/flwb-li/rak3162-zephyr-bsp.git rak3162-zephyr-bsp

cd rak3162-zephyr-bsp
./scripts/bootstrap.sh
# options: --no-sdk  --sdk-dir DIR  --workspace DIR  --build  --retries N
```

The script will:

1. Clear any inherited `ZEPHYR_*` variables from the shell
2. Create a workspace-local Python venv (`.venv`) and install `west` there
3. Initialize a west workspace in the **parent** directory
4. Fetch **Zephyr v4.3.0** + allowlisted modules only: `cmsis`, `cmsis_6`, `hal_nordic`, `loramac-node`  
   (network steps retry up to **5** times with backoff; override with `--retries N`)
5. Install Zephyr Python requirements into `.venv`
6. Download Zephyr SDK **0.17.x** into `<workspace>/zephyr-sdk-0.17.4` (skip with `--no-sdk`)
7. Write `<workspace>/env.sh` for later builds

Then (every new terminal):

```bash
cd ~/rak3162-workspace
source ./env.sh
west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/hw_test --no-sysbuild --pristine always
west flash
```

### Manual west init

Use a **full** `west update` (do **not** pass `--depth=1`). `hal_nordic` is SHA-pinned; shallow fetches often fail to resolve that commit.

```bash
mkdir rak3162-workspace && cd rak3162-workspace
west init -m https://github.com/flwb-li/rak3162-zephyr-bsp.git --mr main rak3162-zephyr-bsp
cd rak3162-zephyr-bsp
west update
west zephyr-export

west build -b rak3162/nrf54l15/cpuapp samples/hw_test --no-sysbuild --pristine always
west flash
```

Workspace layout after `west update`:

```
rak3162-workspace/
├── .venv/                            # west + Python deps (workspace-local)
├── env.sh                            # source this before build
├── zephyr-sdk-0.17.4/                # installed by bootstrap (default)
├── rak3162-zephyr-bsp/               # this BSP (manifest)
│   ├── boards/
│   └── samples/
├── zephyr/                           # Zephyr v4.3.0
└── modules/
    ├── hal/cmsis
    ├── hal/cmsis_6
    ├── hal/nordic
    └── lib/loramac-node
```

If this directory is already cloned, point west at it from the parent workspace:

```bash
# from workspace root, with rak3162-zephyr-bsp/ as the clone of this repo
west init -l rak3162-zephyr-bsp
west update
```

## Mode 2 — Existing Zephyr environment (external module)

If you already have a Zephyr **v4.3.0** workspace, use this BSP as an **external module**.
**No need to copy** boards/samples into the Zephyr tree.

1. Clone the BSP anywhere:

```bash
git clone https://github.com/flwb-li/rak3162-zephyr-bsp.git /path/to/rak3162-zephyr-bsp
```

2. Point Zephyr at it (one of the following):

```bash
# Option A: environment variable (simplest)
export ZEPHYR_EXTRA_MODULES="/path/to/rak3162-zephyr-bsp"

# Option B: CMake argument for a single build
west build -b rak3162/nrf54l15/cpuapp /path/to/rak3162-zephyr-bsp/samples/hw_test \
  -- -DZEPHYR_EXTRA_MODULES=/path/to/rak3162-zephyr-bsp
```

`zephyr/module.yml` registers `board_root`, so board `rak3162/nrf54l15/cpuapp` becomes available.

3. Build and flash (sample stays in the BSP tree):

```bash
west build -b rak3162/nrf54l15/cpuapp /path/to/rak3162-zephyr-bsp/samples/hw_test --no-sysbuild
west flash
```

You can also add the BSP as a west project in your own `west.yml` (`path` + `url`/`remote`) so it is fetched next to your other modules; still no in-tree copy is required.

## Flash

RAK3162 is programmed over **SWD** (nRF54L15). Connect a debug probe to the board’s SWD pads/header:

| Probe signal | Board |
|--------------|--------|
| SWDIO | SWDIO |
| SWCLK | SWCLK |
| GND | GND |
| RESET (recommended) | nRESET / RESET |
| VTref (if required by probe) | 3.3 V |

Power the board (USB or external 3.3 V as applicable) before flashing. Keep SWD wires short.

### Windows + WSL / Linux VM (usbipd)

If you build and flash from **WSL2** (or another Linux environment on Windows), the debug probe and USB–UART adapter are attached to Windows first. Pass them into Linux with **[usbipd-win](https://github.com/dorssel/usbipd-win)** before `west flash` or opening a serial port.

1. On Windows, install usbipd-win (e.g. `winget install dorssel.usbipd-win`).
2. Plug in the probe (and/or USB–UART). Keep a WSL terminal open so the distro stays running.
3. In **PowerShell** (Administrator for `bind`):

```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

Replace `<BUSID>` with the probe’s bus id from `usbipd list` (for example `1-3`).

4. In WSL, confirm the device is visible, then flash:

```bash
lsusb
west flash
```

Detach when finished (PowerShell):

```powershell
usbipd detach --busid <BUSID>
```

Notes:

- Attach is not permanent: re-run `usbipd attach` after reboot, unplug/replug, or if the probe resets.
- While attached to WSL, Windows applications cannot use that USB device.
- The same steps apply to the USB–UART adapter used for the AT console.
- See also [Connect USB devices (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/wsl/connect-usb).

### Tooling

Default flash runner in `boards/rak3162/board.cmake` is **J-Link** (`nRF54L15_M33`). pyOCD and nRF Util are also supported.

| Runner | Install | Typical use |
|--------|---------|-------------|
| **J-Link** (default) | [SEGGER J-Link software](https://www.segger.com/downloads/jlink/) | SEGGER J-Link / J-Link OB |
| **pyOCD** | `pip install pyocd` (use the workspace `.venv` after Mode 1 bootstrap) | CMSIS-DAP / DAPLink / many low-cost probes |
| **nrfutil** | [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util) | Nordic tooling / some DK setups |

On Linux (including WSL after usbipd attach), install udev rules so the probe is usable without root. A starter rule is in `boards/rak3162/support/99-rak3162.rules` (J-Link vendor `1366`). Adjust `idVendor`/`idProduct` for your probe (`lsusb`), then:

```bash
sudo cp boards/rak3162/support/99-rak3162.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Flash after build

From the workspace root (after `source ./env.sh` in Mode 1):

```bash
# Uses the latest build directory (default: ./build)
west flash

# Or point at an explicit build dir
west flash -d build
```

Override the runner when needed:

```bash
west flash --runner pyocd
west flash --runner nrfutil
```

Recover / mass-erase if the chip no longer responds (probe-specific; example with nrfutil):

```bash
west flash --runner nrfutil --erase
```

### Serial console (after flash)

AT console is on **UART20** (Zephyr console): **115200 8N1**, **TX=P1.06**, **RX=P1.07**.

Connect a USB–UART adapter (3.3 V logic), open a terminal, and reset the board. You should see boot logs and can type AT commands:

```text
AT
AT+VER=?
```

On WSL, attach the USB–UART with `usbipd` as above, then use a Linux serial tool (for example `minicom`, `picocom`, or `screen /dev/ttyACM0 115200`).

Full AT list: `samples/hw_test/doc/AT_COMMANDS.md`.  
Pin tables: `boards/rak3162/doc/hardware_pins.md`.

## LoRaWAN region

Default **`AT+BAND=4` (EU868)**. Multiple `CONFIG_LORAMAC_REGION_*` are enabled in `samples/hw_test/prj.conf` so `AT+BAND` can switch region **before** the first `AT+JOIN`. AS923-2/3/4 and LA915 are not available in Zephyr.

## Onboard SX1262

Radio is defined in `boards/rak3162/rak3162_nrf54l15_cpuapp.dts` (SPI `spi22` / `rak_lora_spi`, CS P1.12, RESET P0.04, BUSY P1.13, DIO1 P0.01, DIO2 RF switch, DIO3 TCXO). Alias: `lora0`.

## Version

**1.0.0** — see [CHANGELOG.md](CHANGELOG.md). This BSP is **not** yet upstreamed into Zephyr.

## License

Licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).

Copyright (c) 2026 RAKwireless Technology Limited.
