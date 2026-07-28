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
| `west.yml` | West manifest (Zephyr v4.3.0 + minimal modules) |
| `scripts/bootstrap.sh` | Mode 1: fetch Zephyr/modules (+ optional SDK) for this BSP |
| `scripts/install_into_zephyr.sh` | Mode 2: copy boards/samples into an existing Zephyr tree |
| `scripts/uninstall_from_zephyr.sh` | Mode 2: remove installed files |

## Requirements

On a **clean** machine (no Zephyr / west / SDK preinstalled):

- `git`, `python3` **3.10+**, `cmake`, `ninja`, `wget` or `curl`, `tar`
- Network access to GitHub (Zephyr + SDK download)

`./scripts/bootstrap.sh` installs west, Python deps, and Zephyr SDK into the workspace.

## Mode 1 — West module / manifest (recommended)

### One-shot bootstrap (customers, clean PC)

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

```bash
mkdir rak3162-workspace && cd rak3162-workspace
west init -m https://github.com/flwb-li/rak3162-zephyr-bsp.git --mr main rak3162-zephyr-bsp
cd rak3162-zephyr-bsp
west update -o=--depth=1 -n
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

### Attach to an existing Zephyr workspace

Pin Zephyr to **v4.3.0**, then:

```bash
export ZEPHYR_EXTRA_MODULES="/path/to/rak3162-zephyr-bsp"
west build -b rak3162/nrf54l15/cpuapp /path/to/rak3162-zephyr-bsp/samples/hw_test
```

## Mode 2 — Install into a local Zephyr tree

For customers who already have Zephyr v4.3.0 and do not want to change the west manifest:

```bash
./scripts/install_into_zephyr.sh /path/to/zephyr
# optional: --dry-run  --force

west build -b rak3162/nrf54l15/cpuapp samples/rak/hw_test
```

Uninstall:

```bash
./scripts/uninstall_from_zephyr.sh /path/to/zephyr
```

## LoRaWAN region

Default **`AT+BAND=4` (EU868)**. Multiple `CONFIG_LORAMAC_REGION_*` are enabled in `samples/hw_test/prj.conf` so `AT+BAND` can switch region **before** the first `AT+JOIN`. AS923-2/3/4 and LA915 are not available in Zephyr.

## Onboard SX1262

Radio is defined in `boards/rak3162/rak3162_nrf54l15_cpuapp.dts` (SPI `spi22` / `rak_lora_spi`, CS P1.12, RESET P0.04, BUSY P1.13, DIO1 P0.01, DIO2 RF switch, DIO3 TCXO). Alias: `lora0`.

Do **not** pass `--shield rak_sx1262`.

## Serial and pins

See `doc/hardware_pins.md` and `boards/rak3162/doc/hardware_pins.md`.
AT command reference: `samples/hw_test/doc/AT_COMMANDS.md`.

## Version

See [CHANGELOG.md](CHANGELOG.md). This BSP is **not** yet upstreamed into Zephyr.
