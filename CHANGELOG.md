# Changelog

Customer preview BSP for RAK3162. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- Customer preview tree at this repository root
- Onboard SX1262 in `boards/rak3162` device tree (no shield)
- Mode 1: West manifest (`west.yml`) + Zephyr module (`zephyr/module.yml`)
- Mode 2: `scripts/install_into_zephyr.sh` / `uninstall_from_zephyr.sh`
- `samples/hw_test` AT firmware with LoRaWAN OTAA (`AT+APPKEY` / `AT+JOIN` / `AT+SEND`, downlink `+EVT:RX`)

### Changed

- SX1262 uses Zephyr `semtech,sx1262` + `lora0` alias (loramac-node / `CONFIG_LORAWAN`)
- Removed Semtech USP / `usp_zephyr` from `west.yml` and sample build
- Removed LoRa P2P AT commands (`P2P` / `PRECV` / `PSEND` / `CW`)
