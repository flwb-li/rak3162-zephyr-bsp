# Changelog

RAK3162 Zephyr BSP. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- Board/sample tree at this repository root
- Onboard SX1262 in `boards/rak3162` device tree (no shield)
- Mode 1: West manifest (`west.yml`) + Zephyr module (`zephyr/module.yml`)
- Mode 2: `scripts/install_into_zephyr.sh` / `uninstall_from_zephyr.sh`
- Mode 1 bootstrap: `scripts/bootstrap.sh` (clean-PC: venv + west update + SDK + `env.sh`)
- `west.yml` imports only `cmsis` / `cmsis_6` / `hal_nordic` / `loramac-node` (enough for `samples/hw_test`)
- `samples/hw_test` AT sample: LoRaWAN OTAA Class A + LoRa P2P (`AT+NWM` / `AT+BAND` / `AT+JOIN` / `AT+SEND` / `+EVT:RX_1:...`)
- LoRa P2P AT commands over Zephyr `CONFIG_LORA` (`AT+P2P` / `AT+PRECV` / `AT+PSEND` / `AT+CW`)
- RUI3-inspired JOIN parameters (stop / auto-join / retry interval / attempts)

### Changed

- SX1262 uses Zephyr `semtech,sx1262` + `lora0` alias (loramac-node / `CONFIG_LORAWAN`)
- Removed Semtech USP / `usp_zephyr` from `west.yml` and sample build
- P2P no longer depends on USP; mutually exclusive with an active LoRaWAN stack session
- Board TCXO startup delay set to 30 ms
- Docs: Mode 2 is external module via `ZEPHYR_EXTRA_MODULES` (copy-into-tree is optional)
