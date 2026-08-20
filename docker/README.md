# RAK3162 Zephyr Docker environment

Optional. The BSP README recommends **Workbench for Zephyr** or **IDE for Zephyr**
in VS Code; this image is for CLI builds without a local SDK.

Lives in this BSP at `docker/`. Host needs **Docker** only (no local Zephyr SDK).

The container bind-mounts the **west workspace root** (parent of `rak3162-zephyr-bsp/`),
so `zephyr/`, `modules/`, and this BSP stay siblings on the host.

## Windows (Docker Desktop)

The image is **Linux** (`ubuntu:22.04` + Zephyr SDK **linux-x86_64**). On Windows use
**Docker Desktop with the WSL2 backend**.

If the PC has **no WSL2** (e.g. some Windows Home setups), skip Docker and use
**Mode 1 (VS Code)** in the BSP [`README.md`](../README.md).

| Do | Avoid |
|----|--------|
| Put the west workspace in the **WSL** home (e.g. `~/workspace`) | Long-term builds from `/mnt/c/...` (slow / flaky mounts) |
| Run compose from a **WSL** terminal | Relying on CRLF for `entrypoint.sh` (must stay **LF**) |
| Flash with J-Link / nRF Util / pyOCD on the **Windows host** | Expecting USB SWD flash from inside the container |

AMD64/Intel Windows PCs are the tested path; ARM64 Windows is not covered by this Dockerfile.

## Build the image

From the west workspace root (Linux or WSL):

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
