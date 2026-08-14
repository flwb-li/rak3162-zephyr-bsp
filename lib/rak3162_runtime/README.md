# RAK3162 runtime

Board-specific adapters for thin RAK3162 applications:

- NVS configuration (`rak3162_storage_*`)
- UART RX Sense, bus power management and LEDs
- Binding board/radio ops into `rak-fw` (`radio_bind_at`)

LoRaWAN/P2P services and the AT framework live in `modules/rak-fw`.
Enable with `CONFIG_RAK3162_RUNTIME=y` (requires `CONFIG_RAK_FW` / `CONFIG_RAK_AT`).
