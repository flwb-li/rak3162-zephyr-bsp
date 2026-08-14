# rak-fw — RAK firmware framework (Zephyr module)

Vendored under `rak3162-zephyr-bsp/modules/rak-fw` and **pinned to the BSP git
revision**. Provides:

- RUI3-compatible AT console (`rak_at_*`, `CONFIG_RAK_AT`)
- LoRaWAN / LoRa P2P helpers (`rak_fw_lorawan_*`, `rak_fw_lora_p2p_*`)

Board hooks (LED, bus PM, NVS) come from `rak_fw_board_ops` / `rak_fw_cfg_ops`
in `rak3162_runtime`. Product policy (autojoin, periodic uplink, OTAA defaults)
belongs in the **application**, not this module.

## Enable

```
CONFIG_RAK_FW=y
CONFIG_RAK_AT=y
CONFIG_RAK_FW_LORAWAN=y
CONFIG_RAK_FW_LORA_P2P=y
```

## Discovery

`samples/at_firmware/CMakeLists.txt` appends this directory to
`ZEPHYR_EXTRA_MODULES`. For other apps:

```bash
export ZEPHYR_EXTRA_MODULES=/path/to/rak3162-zephyr-bsp/modules/rak-fw
```

## Tests

```bash
west twister -T modules/rak-fw/tests/util -p qemu_cortex_m3
```
