# RAK3162 HW_TEST AT 指令说明

本文档描述 `samples/hw_test` 固件支持的 AT 指令。  
适用板卡：**RAK3162**（`rak3162/nrf54l15/cpuapp`），板载 SX1262。  
支持 **LoRa P2P**（Zephyr `CONFIG_LORA`）与 **LoRaWAN / loramac-node**（`CONFIG_LORAWAN`）。默认区域 **EU868**。  
P2P 与 LoRaWAN **共用射频**：`AT+JOIN` 前需 `AT+PRECV=0`；LoRaWAN 已启动后 P2P 返回 `AT_BUSY_ERROR`。

---

## 1. 通信接口

| 项目 | 说明 |
|------|------|
| AT 串口 | **UART20**（`zephyr,console`），115200 8N1 |
| TX 引脚 | P1.06 |
| RX 引脚 | P1.07 |
| 行结束 | 响应以 `\r\n` 结尾；主机发送命令建议带 `\r` 或 `\r\n` |
| 回显 | 默认开启（`ATE1` 等效） |

次要 UART（`AT+TEST=UART` 测试用）：**UART21**，TX=P2.08，RX=P2.07。

---

## 2. 通用语法

| 形式 | 含义 | 示例 |
|------|------|------|
| `AT` | 握手 | `AT` → `OK` |
| `AT?` | 列出所有已注册命令 | `AT?` |
| `AT+<CMD>?` | 查看该命令帮助 | `AT+JOIN?` |
| `AT+<CMD>` | 执行命令（无参数） | `AT+JOIN` |
| `AT+<CMD>=?` | 查询当前配置/状态 | `AT+DEVEUI=?` |
| `AT+<CMD>=<args>` | 设置参数或带参执行 | `AT+SEND=2,010203,c` |
| `ATZ` | 冷复位 MCU | `ATZ` |
| `ATE0` / `ATE1` | 关闭/开启串口回显 | `ATE0` |

命令名**不区分大小写**（解析后统一为大写）。

### 2.1 通用响应

| 响应 | 含义 |
|------|------|
| `OK` | 成功 |
| `AT_ERROR` | 未知命令或执行失败 |
| `AT_PARAM_ERROR` | 参数格式/范围错误 |

部分命令会先输出信息行，再输出 `OK`。

LoRa 异步/事件行示例：

- `+EVT:TXP2P DONE` — P2P 发送完成
- `+EVT:RXP2P RECEIVE TIMEOUT` — P2P 接收超时
- `+EVT:RXP2P:<rssi>:<snr>:<hex>` — P2P 收到数据
- `+EVT:JOIN_START` / `+EVT:JOINED` / `+EVT:JOIN_FAILED,<err>`
- `+EVT:SEND_OK`
- `+EVT:RX,PORT=...,RSSI=...,SNR=...,LEN=...,DATA=<hex>` — LoRaWAN 下行

上电**不会自动 join**，需主机执行 `AT+JOIN`。

---

## 3. 系统与设备信息

### 3.1 `ATZ` — 复位

| 项目 | 说明 |
|------|------|
| 功能 | 触发 MCU 冷复位 |
| 格式 | `ATZ` |

### 3.2 `AT+VER` / `AT+BUILDTIME` / `AT+SN`

| 命令 | 说明 |
|------|------|
| `AT+VER=?` | 固件版本字符串 |
| `AT+BUILDTIME=?` | 编译时间 |
| `AT+SN` | 读写 18 字符序列号（Settings 持久化） |

---

## 4. 晶振负载电容（nRF54L 内部电容）

需设备树中 `load-capacitors = "internal"`。设置后立即写硬件并保存。

| 命令 | 范围 | 说明 |
|------|------|------|
| `AT+HFXOCAP` | 4000–17000 fF，步进 250 | HFXO |
| `AT+LFXOCAP` | 4000–18000 fF，步进 500 | LFXO |

---

## 5. LoRa P2P（Zephyr LoRa / SX1262）

P2P 参数保存在 **RAM**，复位后恢复默认值。默认：`868000000:7:125:0:8:14`。

### 5.1 `AT+P2P` — 射频参数

| 项目 | 说明 |
|------|------|
| 查看 | `AT+P2P` 或 `AT+P2P=?` |
| 设置 | `AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<tx_power>` |

| 字段 | 范围 | 说明 |
|------|------|------|
| `freq` | 150000000–960000000 | Hz |
| `sf` | 6–12 | 扩频因子 |
| `bw` | `0`/`125`, `1`/`250`, `2`/`500` | 带宽（kHz；Zephyr 仅支持这三档） |
| `cr` | 0–3 | 0=4/5 … 3=4/8 |
| `preamble` | 2–65535 | 前导码 |
| `tx_power` | 5–22 | dBm |

### 5.2 `AT+PRECV` — 接收

| `time` | 含义 |
|--------|------|
| `0` | 停止接收 |
| `1`–`65532` | 接收窗口（ms） |
| `65533` | 持续接收，仍允许 `PSEND` |
| `65534` | 持续接收，需 `AT+PRECV=0` 退出 |
| `65535` | 收到一包后自动结束 |

### 5.3 `AT+PSEND` — 发送

`AT+PSEND=<hex>`，偶数长度十六进制，1–256 字节。完成后：`+EVT:TXP2P DONE`。

### 5.4 `AT+CW` — 连续波

`AT+CW=<freq>:<power>:<time_ms>`。底层驱动超时单位为**秒**（`time_ms` 向上取整；`0` 表示长时间发射）。

---

## 6. LoRaWAN OTAA（Class A）

凭证与密钥持久化到 Settings。默认区域 EU868（见 `prj.conf` 中 `CONFIG_LORAMAC_REGION_*`）。

### 6.1 凭证

| 命令 | 长度 | 说明 |
|------|------|------|
| `AT+DEVEUI` | 16 hex（8 字节） | DevEUI |
| `AT+APPEUI` | 16 hex（8 字节） | JoinEUI / AppEUI |
| `AT+APPKEY` | 32 hex（16 字节） | AppKey（必填） |
| `AT+NWKKEY` | 32 hex（16 字节） | NwkKey（可选；未设置时 join 使用 APPKEY） |

示例：

```text
AT+DEVEUI=0123456789ABCDEF
AT+APPEUI=0000000000000000
AT+APPKEY=2B7E151628AED2A6ABF7158809CF4F3C
AT+APPKEY=?
```

### 6.2 `AT+JOIN` — OTAA 入网

| 项目 | 说明 |
|------|------|
| 执行 | `AT+JOIN` — `lorawan_start`（若未启）+ `lorawan_join(OTAA)` |
| 查询 | `AT+JOIN=?` — `joined` / `idle` |
| 前置 | 已设置 DEVEUI、APPEUI、APPKEY；P2P 已停止 |

成功示例：

```text
+EVT:JOIN_START
+EVT:JOINED
OK
```

### 6.3 `AT+SEND` — 上行

| 项目 | 说明 |
|------|------|
| 格式 | `AT+SEND=<port>,<hex>[,c\|u]` |
| `port` | 1–223 |
| `hex` | 偶数长度十六进制载荷（最长 128 字节） |
| `c` | confirmed（默认） |
| `u` | unconfirmed |

示例：

```text
AT+SEND=2,010203,c
+EVT:SEND_OK
OK
```

未 join 时返回 `AT_ERROR: not joined`。

### 6.4 `AT+CLASS=?` — 查询 Class

本 sample 仅 Class A，查询返回 `AT+CLASS=A`。

### 6.5 下行

栈在 join 前注册下行回调。收到数据时串口打印：

```text
+EVT:RX,PORT=2,RSSI=-80,SNR=5,LEN=3,DATA=AA55FF
```

---

## 7. 低功耗

### 7.1 `AT+RTC` / `AT+SLEEP`

| 命令 | 说明 |
|------|------|
| `AT+RTC=<s>` | 设置下次 System OFF 的 GRTC 唤醒秒数；`0` 关闭 |
| `AT+SLEEP=<delay_ms>` | 延时后 System OFF |

---

## 8. 外设与 BLE 测试

### 8.1 `AT+TEST`

| 子命令 | 功能 |
|--------|------|
| `IIC` / `SPI` / `UART` | 外设波形 |
| `BLE` | BLE 广播 |
| `GRTC` / `GRTCSTOP` | GRTC 方波（需 overlay） |

### 8.2 `AT+BLECW` / `AT+BLECWSTOP`

BLE 单载波（RADIO 外设）。`AT+BLECW=<ch>[,<pwr_dbm>]`，信道频率 = 2400 + ch MHz。

---

## 9. 典型流程

### 9.1 P2P

```text
AT+P2P=868000000:7:125:0:8:14
AT+PRECV=65535
# 对端：
AT+PSEND=48656C6C6F
# 本端：
# +EVT:RXP2P:<rssi>:<snr>:48656C6C6F
```

### 9.2 LoRaWAN：密钥 → JOIN → SEND → 下行

```text
AT
AT+DEVEUI=<16 hex>
AT+APPEUI=<16 hex>
AT+APPKEY=<32 hex>
AT+JOIN
AT+JOIN=?
AT+SEND=2,48656C6C6F,c
# 网关/NS 下发后可见：
# +EVT:RX,PORT=...,DATA=...
```

---

## 10. 构建

```bash
west build -b rak3162/nrf54l15/cpuapp samples/hw_test --no-sysbuild
```

无需 `--shield`。换区域请修改 `prj.conf` 中的 `CONFIG_LORAMAC_REGION_*`。

---

## 11. 命令速查表

| 命令 | 类型 | 持久化 |
|------|------|--------|
| `AT` / `AT?` / `ATZ` / `ATE0`/`ATE1` | 系统 | — |
| `AT+VER` / `AT+BUILDTIME` | 只读 | — |
| `AT+SN` | 读写 | 是 |
| `AT+HFXOCAP` / `AT+LFXOCAP` | 读写 | 是 |
| `AT+P2P` / `AT+PRECV` / `AT+PSEND` / `AT+CW` | LoRa P2P | RAM |
| `AT+DEVEUI` / `AT+APPEUI` / `AT+APPKEY` / `AT+NWKKEY` | 读写 | 是 |
| `AT+JOIN` / `AT+SEND` / `AT+CLASS` | LoRaWAN | — |
| `AT+SLEEP` / `AT+RTC` | 低功耗 | RTC 仅 RAM |
| `AT+TEST` | 动作 | — |
| `AT+BLECW` / `AT+BLECWSTOP` | 动作/查询 | 否 |
