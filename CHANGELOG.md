# Changelog

Customer preview BSP for RAK3162. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- Customer preview tree at this repository root
- Onboard SX1262 in `boards/rak3162` device tree (no shield)
- Mode 1: West manifest (`west.yml`) + Zephyr module (`zephyr/module.yml`)
- Mode 2: `scripts/install_into_zephyr.sh` / `uninstall_from_zephyr.sh`
- `samples/hw_test` hardware test application

### Changed

- SX1262 is board-integrated; build without `--shield rak_sx1262`
