# RAK3162 Zephyr Docker environment

Lives in this BSP at `docker/`. Host needs **Docker** only (no local Zephyr SDK).

The container bind-mounts the **west workspace root** (parent of `rak3162-zephyr-bsp/`),
so `zephyr/`, `modules/`, and this BSP stay siblings on the host.

## Build the image

From the west workspace root:

```bash
docker compose -f rak3162-zephyr-bsp/docker/compose.yaml build
```

Image tag: `rak3162-zephyr:4.3.0-sdk0.17.4` (Zephyr SDK **0.17.4**, tools for Zephyr **v4.3.0**).

## Build firmware

```bash
docker compose -f rak3162-zephyr-bsp/docker/compose.yaml run --rm build \
  west build -b rak3162/nrf54l15/cpuapp rak3162-zephyr-bsp/samples/at_firmware -d build
```

Output appears under `./build` on the host workspace root.

## Flash

Flash from the **host** with J-Link / nRF Util / pyOCD (USB). Use
`build/zephyr/zephyr.hex` (or `.elf`).

## Modules

`rak-fw` lives at `rak3162-zephyr-bsp/modules/rak-fw` (pinned with the BSP).
The sample CMakeLists injects it; the container entrypoint also sets
`ZEPHYR_EXTRA_MODULES` to that path for convenience.
