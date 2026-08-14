# RAK3162 Zephyr BSP

Board support package for **RAK3162** (nRF54L15 + onboard **SX1262**).
Based on **Zephyr v4.3.0** and West.

**Repository:** https://github.com/flwb-li/rak3162-zephyr-bsp

Product / field AT firmware: OTAA Class A, auto-join, configurable counter uplinks
(`AT+SENDINT`, default **10 s**, `0` = off), **System ON idle** between RF windows
(`AT+SLEEP` = System OFF). SX1262 is in the board DTS — **no `--shield`**.

| Doc | Path |
|-----|------|
| AT commands | `samples/at_firmware/doc/AT_COMMANDS.md` |
| Low power | `samples/at_firmware/doc/LOW_POWER.md` |
| Firmware overview (ZH) | `samples/at_firmware/doc/固件说明.md` |
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
export ZEPHYR_EXTRA_MODULES="/path/to/rak3162-zephyr-bsp/modules/rak-fw"
```

## Defaults / first boot

| Item | Behavior |
|------|----------|
| OTAA seed | Temporary test keys in `samples/at_firmware/src/main.c` (`APP_OTAA_*_HEX`); seeded only when NVS has no credentials |
| Change keys | `AT+DEVEUI` / `AT+APPEUI` / `AT+APPKEY` (persisted) |
| Auto uplink | `AT+SENDINT` (default `10`; `0` disables) — Port 2, 4-byte BE FCnt, unconfirmed |
| ADR | ON by default |
| Boot print | None (`CONFIG_LOG=n`); send `AT` after reset to confirm liveness |

## Flash / console

- Flash **`build/zephyr/zephyr.hex`** over **SWD** from the host.
- AT UART: TX=P1.06, RX=P1.07, **115200 8N1**.

See `samples/at_firmware/doc/AT_COMMANDS.md` for the full command set.
