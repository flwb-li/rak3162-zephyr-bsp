# RAK3162 HW_TEST AT 指令说明

本文档描述 `samples/hw_test` 固件支持的 AT 指令。  
适用板卡：**RAK3162**（`rak3162/nrf54l15/cpuapp`），板载 SX1262。  

面向**实际使用**（OTAA / 上下行 / P2P），不是完整 RUI3 模组固件。  
命令风格参考 [RUI3 AT Command Manual](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/)，仅为实用子集：未实现 ABP、Class B/C、P2P_FSK、LPSEND、LINKCHECK、MASK/TXP 等。

---

## 1. 通信接口

| 项目 | 说明 |
|------|------|
| AT 串口 | **UART20**（`zephyr,console`），115200 8N1 |
| TX / RX | P1.06 / P1.07 |
| 行结束 | 响应以 `\r\n` 结尾 |
| 回显 | 默认开启（`ATE1`） |

次要 UART（`AT+TEST=UART`）：**UART21**，TX=P2.08，RX=P2.07。

---

## 2. 通用语法与状态码

| 形式 | 含义 |
|------|------|
| `AT+<CMD>?` | 帮助 |
| `AT+<CMD>` | 执行 |
| `AT+<CMD>=?` | 查询 |
| `AT+<CMD>=<args>` | 设置 |

| 状态 | 含义 |
|------|------|
| `OK` | 成功（异步命令表示**已启动**） |
| `AT_ERROR` | 通用错误 |
| `AT_PARAM_ERROR` | 参数错误 |
| `AT_BUSY_ERROR` | 忙（join/send/P2P RX 进行中等） |
| `AT_NO_NETWORK_JOINED` | 未入网 |
| `AT_NO_CLASSB_ENABLE` | 不支持 Class B/C |

异步事件（UART 随时可能出现）：

| 事件 | 说明 |
|------|------|
| `+EVT:JOINED` | OTAA 成功 |
| `+EVT:JOIN FAILED` | OTAA 失败 |
| `+EVT:TX_DONE` | 上行发送完成 |
| `+EVT:SEND_CONFIRMED_OK` / `FAILED` | 确认上行结果（`AT+CFM=1`） |
| `+EVT:RX_1:<rssi>:<snr>:UNICAST:<port>:<hex>` | Class A 下行 |
| `+EVT:TXP2P DONE` | P2P 发送完成 |
| `+EVT:RXP2P:<rssi>:<snr>:<hex>` | P2P 收到数据 |
| `+EVT:RXP2P RECEIVE TIMEOUT` | P2P 接收超时 |

---

## 3. 网络工作模式与频段

### 3.1 `AT+NWM` — 工作模式

| 值 | 含义 |
|----|------|
| `0` | P2P_LORA |
| `1` | LoRaWAN（默认） |
| `2` | P2P_FSK（**不支持** → `AT_PARAM_ERROR`） |

切换时会停止 P2P RX；**不会**像 RUI3 那样自动整机复位。持久化到 Settings。

P2P 写操作（`P2P`/`PRECV`/`PSEND`/`CW` SET）要求 `AT+NWM=0`。  
LoRaWAN `JOIN`/`SEND` 要求 `AT+NWM=1`。

### 3.2 `AT+BAND` — 区域

RUI3 编号：

| 值 | 区域 | 本固件 |
|----|------|--------|
| 0 | EU433 | 支持 |
| 1 | CN470 | 支持 |
| 2 | RU864 | 支持 |
| 3 | IN865 | 支持 |
| 4 | EU868 | 支持（默认） |
| 5 | US915 | 支持 |
| 6 | AU915 | 支持 |
| 7 | KR920 | 支持 |
| 8 | AS923-1 | 支持（映射 Zephyr AS923） |
| 9–12 | AS923-2/3/4、LA915 | **不支持** → `AT_PARAM_ERROR` |

须在 **LoRaWAN stack 启动前**（首次 `AT+JOIN` 前）设置；启动后改 BAND → `AT_BUSY_ERROR`。

硬件射频频段需与所选 region 匹配（LF/HF 模块差异）。

---

## 4. LoRaWAN Keys / Join / Send

### 4.1 凭证

| 命令 | 说明 |
|------|------|
| `AT+DEVEUI` | 16 hex |
| `AT+APPEUI` | 16 hex（JoinEUI） |
| `AT+APPKEY` | 32 hex |
| `AT+NWKKEY` | 32 hex（扩展；未设则 join 用 APPKEY） |

### 4.2 `AT+NJM` / `AT+NJS` / `AT+CFM` / `AT+CFS` / `AT+ADR` / `AT+CLASS` / `AT+RECV`

| 命令 | 说明 |
|------|------|
| `AT+NJM` | 仅 OTAA(=1)；ABP → `AT_PARAM_ERROR` |
| `AT+NJS=?` | `0` 未入网 / `1` 已入网 |
| `AT+CFM` | `0`/`1`，控制 `AT+SEND` 是否确认（默认 0） |
| `AT+CFS=?` | 上次 confirmed 发送是否成功 |
| `AT+ADR` | `0`/`1` ADR |
| `AT+CLASS` | 仅 Class A；B/C → `AT_NO_CLASSB_ENABLE` |
| `AT+RECV=?` | 上次下行 `<port>:<hex>`；读后清空为 `0:` |

### 4.3 `AT+JOIN`

对齐 [RUI3 AT+JOIN](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/)：

```text
AT+JOIN?
AT+JOIN=?
AT+JOIN
AT+JOIN=1:0:10:8
AT+JOIN=0
```

| 参数 | 含义 | 默认 |
|------|------|------|
| Param1 | `1`=开始 join，`0`=停止 join | — |
| Param2 | 上电自动 join（`0`/`1`） | `0` |
| Param3 | 重试间隔秒（7–255） | `8` |
| Param4 | 尝试次数；`0`=一直重试直到成功或 `AT+JOIN=0` | `0` |

行为：
- 异步：立即 `OK`，完成时 `+EVT:JOINED` 或（次数用尽）`+EVT:JOIN FAILED`
- 中间失败不报 EVT，按 Param3 间隔重试（与 RUI3 一致）
- `AT+JOIN=?` 在 joining 期间返回 `AT_BUSY_ERROR`
- 入网状态用 `AT+NJS=?`，不要用 `JOIN=?`

### 4.4 `AT+SEND`

```text
AT+SEND=<port>:<payload>
```

例：`AT+SEND=2:010203`  
立即 `OK`，随后 `+EVT:TX_DONE`；若 `CFM=1` 再跟 `SEND_CONFIRMED_OK` / `FAILED`。

---

## 5. LoRa P2P

默认参数（RAM）：`868000000:7:125:0:8:14`。需 `AT+NWM=0`。

| 命令 | 说明 |
|------|------|
| `AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<power>` | bw: 0/125,1/250,2/500 |
| `AT+PRECV=<time>` | 0 / 1..65532 / 65533 / 65534 / 65535（同 RUI3） |
| `AT+PSEND=<hex>` | 发送后 `+EVT:TXP2P DONE` |
| `AT+CW=<freq>:<power>:<time_ms>` | 连续波 |

---

## 6. 典型流程

### LoRaWAN 上下行

```text
AT+NWM=1
AT+BAND=4
AT+DEVEUI=<16 hex>
AT+APPEUI=<16 hex>
AT+APPKEY=<32 hex>
AT+CFM=1
AT+JOIN
OK
+EVT:JOINED
AT+NJS=?
AT+NJS=1
OK
AT+SEND=2:48656C6C6F
OK
+EVT:TX_DONE
+EVT:SEND_CONFIRMED_OK
+EVT:RX_1:-80:5:UNICAST:2:AA55FF
AT+RECV=?
AT+RECV=2:AA55FF
OK
```

### P2P

```text
AT+NWM=0
AT+P2P=868000000:7:125:0:8:14
AT+PRECV=65535
AT+PSEND=48656C6C6F
```

---

## 7. 其他命令（非 LoRa）

`AT` / `AT?` / `ATZ` / `ATE0`/`ATE1`、`AT+VER` / `AT+BUILDTIME` / `AT+SN`、`AT+HFXOCAP` / `AT+LFXOCAP`、`AT+SLEEP` / `AT+RTC`、`AT+TEST`、`AT+BLECW` / `AT+BLECWSTOP` — 行为同前。

---

## 8. 构建

```bash
west build -b rak3162/nrf54l15/cpuapp samples/hw_test --no-sysbuild
```
