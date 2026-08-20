# Changelog

All notable changes to the RAK3162 Zephyr BSP are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [1.0.2] - 2026-08-20

Optional unpublished factory extras as an external Zephyr module (not in this
sample). `AT+VER` reports `V_1.0.2`.

### Added

- `samples/at_firmware` optionally pulls an external extras module before Zephyr
  configure (`RAK3162_AT_EXTRAS_DIR`, or west-workspace sibling
  `rak3162-at-factory/`). Public clones without that module build product
  firmware only
- `help=NULL` on `rak_at_register_command()` omits the command from `AT?`
  (still executable). Used by unpublished extras
- Weak `app_extras_init()` / `app_keep_awake()` hooks so extras can register
  and hold UART/buses without product AT commands in the published tree

### Changed

- `SOFTWARE_VERSION` → `V_1.0.2` (`lib/rak3162_runtime/src/config.h`, `AT+VER`)
- Product `AT_COMMANDS.md`: CW is not a published product command; UART1 pin note
  no longer mentions `AT+TEST`
- README: recommend VS Code **Workbench for Zephyr** or **IDE for Zephyr**;
  west CLI and Docker remain alternatives. Dropped the Windows-native
  (no WSL) CLI walkthrough — use Mode 1 on Windows

## [1.0.1] - 2026-08-19

Firmware test follow-up: AT parameter hardening, System OFF sleep/wake, and
`AT+SENDINT=0` stop reliability. `AT+VER` reports `V_1.0.1`.

### Fixed

- `AT+BAND` illegal values (`99` / `9` / `0` / empty / trailing junk) now return
  `AT_PARAM_ERROR` instead of a sticky `AT_BUSY_ERROR` (validate before busy)
- `AT+BAND` after `AT+JOIN=0:0` (stack started but join idle) now returns `OK`
  instead of `AT_BUSY_ERROR`; region is reapplied via LoRaMAC reinit when needed
- `AT+BAND` while idle-joined (e.g. after `AT+SENDINT=0`) now returns `OK`,
  drops the session (`NJS=0`) and re-inits the region; still `AT_BUSY_ERROR`
  during Join/Send
- Added `AT+DEVADDR` (RUI3): GET returns NS DevAddr when joined, else stored NVS;
  SET persists 8 hex digits (ABP join still not implemented)
- `AT+BAND` now takes effect on the radio: LoRaMAC NVM no longer restores the
  previous region's EU868 (etc.) channels after a region change
- Periodic `+EVT:TX_DONE` no longer prints while AT UART is in Sense/suspend
  (garbled `+Q...` lines); events are emitted before UART LP enter
- OTAA join no longer waits forever for MLME (20 s timeout + MAC reset) after
  a radio/DIO1 stall; `AT+JOIN=1:0:8:0` still retries until stop (no JOIN FAILED)
- `AT+JOIN` after `AT+BAND` no longer hangs silently: `SX126xWakeup()` runs on
  every RF window when the stack is started (DIO1 re-enabled after MAC deinit)
- `AT+NWM=0` now deinitializes LoRaMAC and clears the session so P2P can run and
  `AT+NJS` returns 0 (was stuck Busy / NJS=1 after a prior Join)
- `AT+JOIN=0` during an in-flight OTAA attempt discards a late Join success (no
  `+EVT:JOINED` / session keep) once `lorawan_join()` returns
- `AT+SLEEP` now stops `SENDINT` / join-retry before System OFF; Join/Send/P2P
  in progress returns `AT_BUSY_ERROR`
- `AT+RTC` is armed **after** `AT+SLEEP` delay and immediately before
  `sys_poweroff`, so the wake interval starts at deep-sleep entry (not at `OK`)
- `AT+JOIN=1` could HardFault immediately after `OK` (`BFAR=0x4` in
  `RegionEU868InitDefaults` RESET): re-bind region NVM pointers from params
  before touching channels; resume RF/SPI before `lorawan_start`; enlarge
  LoRaWAN work-queue stack
- `AT+CFM` / `AT+ADR` / `AT+NWM` / `AT+NJM` / `AT+JOIN` / `AT+SEND` /
  `AT+SLEEP` / `AT+RTC` / `AT+SENDINT` / `AT+PRECV` reject empty args, signs,
  and trailing junk via `rak_at_parse_ulong()`

### Changed

- `SOFTWARE_VERSION` → `V_1.0.1` (`lib/rak3162_runtime/src/config.h`, `AT+VER`)
- `AT+SLEEP` help notes that entering System OFF stops the auto-uplink cycle
- `LOW_POWER.md` / `AT_COMMANDS.md`: System OFF prep and RTC-arm timing
- README: Windows native setup pitfalls; `west flash` (default J-Link)

## [1.0.0] - 2026-08-14

First public release of the RAK3162 Zephyr BSP.

Based on **Zephyr v4.3.0**. Intended for practical evaluation and application
development (OTAA Class A + LoRa P2P). Not a full RUI3 firmware replacement.

### Added

- Board support for **RAK3162** (`rak3162/nrf54l15/cpuapp`) with onboard SX1262
  (`semtech,sx1262`, alias `lora0`; no shield required)
- Product AT firmware `samples/at_firmware`: UART AT console, auto-join,
  `AT+SENDINT` counter uplinks, System ON idle (`AT+SLEEP` = System OFF)
- Vendored framework `modules/rak-fw` (AT + LoRaWAN/P2P; sample CMake injects it)
- Board adapters `lib/rak3162_runtime` (NVS, PM, LED, Sense, radio bind)
- In-tree `docker/` (Zephyr SDK 0.17.4); compose mounts the west workspace parent
- LoRaWAN (Zephyr `CONFIG_LORAWAN` / loramac-node): OTAA Class A, `AT+NWM` /
  `AT+BAND` / `AT+JOIN` / `AT+SEND` / `AT+RECV`, downlink `+EVT:RX_1:...`
- LoRa P2P (Zephyr `CONFIG_LORA`): `AT+P2P` / `AT+PRECV` / `AT+PSEND` / `AT+CW`
- Practical AT command subset (RUI3-inspired); see `samples/at_firmware/doc/AT_COMMANDS.md`
- Docs: Docker-first README (incl. Windows / WSL2 notes), Mode 2 native Windows
  toolchain install (no Docker), `AT_COMMANDS.md`, `LOW_POWER.md`
- West workspace via `west.yml` (minimal modules: `cmsis`, `cmsis_6`,
  `hal_nordic`, `loramac-node`); optional deprecated `scripts/bootstrap.sh`
- Hardware pin notes under `boards/rak3162/doc/`

### Changed

- Product policy (autojoin / SENDINT / OTAA seed) lives in `samples/at_firmware/src/main.c`
- Uplink reschedules after send completion / `-EAGAIN` (duty-cycle aware)
- OTAA defaults in `main.c` (`APP_OTAA_*_HEX`); AT+DEVEUI/APPEUI/APPKEY still override NVS
- Merged AT + LoRaWAN/P2P into `rak-fw` (replaces standalone `rak-at`)
- `rak3162_runtime` is board adapters only (no `src/lora`)
- Renamed product sample `samples/hw_test` → `samples/at_firmware`
- Renamed runtime APIs from `hw_*` to `rak3162_*`
- Settings NVS root default `hwtest` → `rak3162` (existing devices need re-provision)
- `.gitignore` keeps `modules/rak-fw` while ignoring other `modules/*`

### Removed

- Framework-level autosend / autojoin APIs (product policy is application-owned)
- Sample `samples/hw_test` (replaced by `samples/at_firmware`)
