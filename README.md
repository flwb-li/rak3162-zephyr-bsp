# RAK3162 Zephyr BSP

Customer preview BSP for **RAK3162** (nRF54L15) with **onboard SX1262** LoRa radio.
Based on **Zephyr v4.3.0** and West.

**Repository:** https://github.com/flwb-li/rak3162-zephyr-bsp

SX1262 is part of the board device tree — **no `--shield` is required**.

## Contents

| Path | Description |
|------|-------------|
| `boards/rak3162/` | Board support (DTS includes onboard SX1262) |
| `samples/hw_test/` | Hardware test firmware (AT commands, LoRa, BLE CW, …) |
| `zephyr/module.yml` | Registers this tree as a Zephyr module (`board_root`) |
| `west.yml` | West manifest (Zephyr + Semtech USP) |
| `scripts/install_into_zephyr.sh` | Mode 2: copy boards/samples into an existing Zephyr tree |
| `scripts/uninstall_from_zephyr.sh` | Mode 2: remove installed files |

## Requirements

- Python 3.10+
- [West](https://docs.zephyrproject.org/latest/develop/west/index.html)
- Zephyr SDK **0.17.x** (matches Zephyr 4.3)
- Serial tool at **115200** baud

## Mode 1 — West module / manifest (recommended)

```bash
mkdir rak3162-workspace && cd rak3162-workspace
west init -m git@github.com:flwb-li/rak3162-zephyr-bsp.git --mr main rak3162-zephyr-bsp
cd rak3162-zephyr-bsp
west update -o=--depth=1 -n
west zephyr-export

west build -b rak3162/nrf54l15/cpuapp samples/hw_test --no-sysbuild --pristine always
west flash
```

Workspace layout after `west update`:

```
rak3162-workspace/
├── rak3162-zephyr-bsp/               # this BSP (manifest)
│   ├── boards/
│   ├── samples/
│   └── modules/semtech/usp_zephyr/   # from west.yml
├── modules/lib/usp/                  # from west.yml
└── zephyr/                           # Zephyr v4.3.0
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
export ZEPHYR_EXTRA_MODULES="/path/to/rak3162-zephyr-bsp;/path/to/usp_zephyr;/path/to/usp"
west build -b rak3162/nrf54l15/cpuapp /path/to/rak3162-zephyr-bsp/samples/hw_test
```

Or rely on `west.yml` / `ZEPHYR_EXTRA_MODULES` as in `samples/hw_test/CMakeLists.txt`.

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

**Note:** Mode 2 only copies board + sample sources. LoRa still needs Semtech **usp_zephyr** and **usp** (via west or `ZEPHYR_EXTRA_MODULES`).

## Onboard SX1262

Radio is defined in `boards/rak3162/rak3162_nrf54l15_cpuapp.dts` (SPI `spi22` / `rak_lora_spi`, CS P1.12, RESET P0.04, BUSY P1.13, DIO1 P0.01, DIO2 RF switch, DIO3 TCXO).

Do **not** pass `--shield rak_sx1262`.

## Serial and pins

See `doc/hardware_pins.md` and `boards/rak3162/doc/hardware_pins.md`.
AT command reference: `samples/hw_test/doc/AT_COMMANDS.md`.

## Semtech USP

`west.yml` pulls:

- `usp_zephyr` @ `v1.1.2-feature-202604`
- `usp` @ same revision (with submodules)

GitHub access to [Lora-net](https://github.com/Lora-net) is required for `west update`.

## Version

See [CHANGELOG.md](CHANGELOG.md). This preview BSP is **not** yet upstreamed into Zephyr.
