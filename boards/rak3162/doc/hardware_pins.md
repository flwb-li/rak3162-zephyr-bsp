<!--
Copyright (c) 2026 RAKwireless Technology Limited
SPDX-License-Identifier: Apache-2.0
-->

# Hardware pin assignment

## 1. SX1262 LoRa module

| Function | Pin | Description |
|----------|-----|-------------|
| SCK | P1.11 | SPI clock |
| MOSI | P1.10 | SPI master out / slave in |
| MISO | P1.09 | SPI master in / slave out |
| CS/NSS | P1.12 | SPI chip select |
| RESET | P0.04 | SX1262 reset |
| BUSY | P1.13 | Module busy |
| ANT_SW | P0.00 | LoRa antenna switch |
| DIO1 | P0.01 | Interrupt / data ready |
| DIO2 | | LoRa RF TX/RX switch |
| DIO3 | | TCXO 1.8 V |

## 2. UART

| Board name | Zephyr node | TX | RX | Description |
|------------|-------------|----|----|-------------|
| UART0 | `uart20` | P1.06 | P1.07 | Debug / AT console (`zephyr,console`) |
| UART1 | `uart21` | P2.08 | P2.07 | Secondary UART (`AT+TEST=UART`) |

## 3. I²C

| Function | Pin | Description |
|----------|-----|-------------|
| SDA | P0.02 | Data |
| SCL | P0.03 | Clock |

## 4. Reserved SPI

| Function | Pin | Description |
|----------|-----|-------------|
| CS | P2.05 | SPI chip select |
| SCK | P2.01 | SPI clock |
| MISO | P2.04 | SPI master in / slave out |
| MOSI | P2.02 | SPI master out / slave in |

## 5. NFC

| NFC pin | Pin | Description |
|---------|-----|-------------|
| NFC1 | P1.02 | |
| NFC2 | P1.03 | |

## 6. Analog inputs (AIN)

| AIN | Pin | Description |
|-----|-----|-------------|
| AIN0 | P1.04 | Analog input |
| AIN1 | P1.05 | Analog input |

## 7. LED

| LED | Pin | Description |
|-----|-----|-------------|
| LED1 | P2.09 | |
| LED2 | P2.10 | |

## 8. General-purpose GPIO

| GPIO | Pin | Description |
|------|-----|-------------|
| GPIO1 | P1.08 | MCUboot DFU enter (`mcuboot-button0`): hold low through reset |
| GPIO2 | P1.14 | Can also be used as AIN7 |
| GPIO3 | P2.03 | |
| GPIO4 | P2.06 | |

## 9. nRF54L15 HFXO / LFXO

**HFXO:** High-Frequency Crystal Oscillator — external 32 MHz crystal.

**LFXO:** Low-Frequency Crystal Oscillator — external 32.768 kHz crystal.

External crystals need matching load capacitors configured in the board DTS:
[`rak3162_nrf54l15_cpuapp.dts`](../rak3162_nrf54l15_cpuapp.dts).
