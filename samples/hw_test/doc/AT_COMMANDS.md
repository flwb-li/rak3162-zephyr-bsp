# RAK3162 HW_TEST AT 指令说明

本文档描述 `samples/hw_test` 固件支持的 AT 指令。  
适用板卡：**RAK3162**（`rak3162/nrf54l15/cpuapp`），SX1262 为板载射频，无需 --shield。

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
| `AT+<CMD>?` | 查看该命令帮助 | `AT+P2P?` |
| `AT+<CMD>` | 执行命令（无参数） | `AT+TEST=BLE` |
| `AT+<CMD>=?` | 查询当前配置/状态 | `AT+P2P=?` |
| `AT+<CMD>=<args>` | 设置参数或带参执行 | `AT+P2P=868000000:7:125:0:8:14` |
| `ATZ` | 冷复位 MCU | `ATZ` |
| `ATE0` / `ATE1` | 关闭/开启串口回显 | `ATE0` |

命令名**不区分大小写**（解析后统一为大写）。

### 2.1 通用响应

| 响应 | 含义 |
|------|------|
| `OK` | 成功 |
| `AT_ERROR` | 未知命令或执行失败 |
| `AT_PARAM_ERROR` | 参数格式/范围错误 |
| `AT_BUSY_ERROR` | 射频忙（LoRa TX/RX/CW 进行中） |
| `RADIO_BUSY` | `AT+SLEEP` 时 LoRa 无法进入低功耗 |
| `COMMAND_LOCKED` | 命令被锁定（预留，当前未启用密码锁） |

部分命令会先输出信息行（如 `AT+P2P=...`），再输出 `OK`。

LoRa 异步事件（非 AT 响应）示例：

- `+EVT:TXP2P DONE` — P2P 发送完成  
- `+EVT:RXP2P RECEIVE TIMEOUT` — 接收超时  
- 收到数据时会有 `+EVT:RXP2P ...` 类上报（十六进制载荷）

---

## 3. 系统与设备信息

### 3.1 `ATZ` — 复位

| 项目 | 说明 |
|------|------|
| 功能 | 触发 MCU 冷复位 |
| 格式 | `ATZ` |
| 参数 | 无 |
| 响应 | 通常来不及返回 `OK`（设备随即复位） |

---

### 3.2 `AT+VER` — 固件版本

| 项目 | 说明 |
|------|------|
| 功能 | 读取软件版本字符串 |
| 查询 | `AT+VER=?` 或 `AT+VER?` |
| 响应示例 | `AT+VER=V_0.1.0` |

---

### 3.3 `AT+BUILDTIME` — 编译时间

| 项目 | 说明 |
|------|------|
| 功能 | 读取固件编译日期时间 |
| 查询 | `AT+BUILDTIME=?` 或 `AT+BUILDTIME?` |
| 响应示例 | `AT+BUILDTIME=May 19 2026 12:34:56` |

---

### 3.4 `AT+SN` — 序列号

| 项目 | 说明 |
|------|------|
| 功能 | 读写设备序列号（持久化到 Settings） |
| 长度 | **固定 18 字符**，可打印 ASCII（0x20–0x7E） |
| 查询 | `AT+SN=?` — 未设置时返回 `AT+SN=` 后 `OK` |
| 设置 | `AT+SN=0123456789ABCDEF01` |
| 帮助 | `AT+SN?` |

---

## 4. 晶振负载电容（nRF54L 内部电容）

需设备树中 `load-capacitors = "internal"`。设置后立即写硬件寄存器，并保存；下次上电自动恢复。

### 4.1 `AT+HFXOCAP` — 32 MHz 晶振（HFXO）

| 项目 | 说明 |
|------|------|
| 功能 | 设置/查询 HFXO 内部负载电容 |
| 单位 | 飞法（fF） |
| 范围 | **4000–17000**，步进 **250** |
| 板级默认 | 15000 fF（15 pF，见 `rak3162_nrf54l15_cpuapp.dts`） |
| 查询 | `AT+HFXOCAP=?` — 未保存过则 `AT_ERROR` |
| 设置 | `AT+HFXOCAP=15000` |
| 帮助 | `AT+HFXOCAP?` |

---

### 4.2 `AT+LFXOCAP` — 32.768 kHz 晶振（LFXO）

| 项目 | 说明 |
|------|------|
| 功能 | 设置/查询 LFXO 内部负载电容 |
| 单位 | 飞法（fF） |
| 范围 | **4000–18000**，步进 **500** |
| 板级默认 | 16000 fF（16 pF） |
| 查询 | `AT+LFXOCAP=?` |
| 设置 | `AT+LFXOCAP=16000` |
| 帮助 | `AT+LFXOCAP?` |

---

## 5. LoRa P2P（Semtech USP / SX1262）

需板载 SX1262 与 LoRa 栈初始化成功。  
P2P 参数保存在 **RAM**，复位后恢复默认值（除 `DEVEUI`/`APPEUI` 外见下文）。

**默认 P2P 参数**（上电）：`868000000:7:125:0:8:14`  
即 868 MHz、SF7、BW 125 kHz、CR 4/5、前导 8、发射功率 14 dBm。

### 5.1 `AT+DEVEUI` — 设备 EUI

| 项目 | 说明 |
|------|------|
| 功能 | 读写 8 字节 DevEUI（持久化） |
| 格式 | 16 位十六进制字符串 |
| 查询 | `AT+DEVEUI=?` |
| 设置 | `AT+DEVEUI=0123456789ABCDEF` |
| 帮助 | `AT+DEVEUI?` |

---

### 5.2 `AT+APPEUI` — 应用 EUI

| 项目 | 说明 |
|------|------|
| 功能 | 读写 8 字节 AppEUI（持久化） |
| 格式 | 16 位十六进制字符串 |
| 查询 | `AT+APPEUI=?` |
| 设置 | `AT+APPEUI=0123456789ABCDEF` |
| 帮助 | `AT+APPEUI?` |

---

### 5.3 `AT+P2P` — P2P 射频参数

| 项目 | 说明 |
|------|------|
| 功能 | 查看/设置 LoRa P2P 调制参数（仅 RAM，不落盘） |
| 查看 | `AT+P2P` 或 `AT+P2P=?` |
| 设置 | `AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<tx_power>` |

**参数说明**（6 段，冒号分隔）：

| 字段 | 范围 | 说明 |
|------|------|------|
| `freq` | 150000000–960000000 | 频率（Hz），如 `868000000` |
| `sf` | 6–12 | 扩频因子 |
| `bw` | 见下表 | 带宽 |
| `cr` | 0–3 | 编码率：0=4/5, 1=4/6, 2=4/7, 3=4/8 |
| `preamble` | 2–65535 | 前导码长度 |
| `tx_power` | 5–22 | 发射功率（dBm） |

**带宽 `bw` 取值**（可用序号或 kHz 字符串）：

| 值 | 带宽 |
|----|------|
| 0 或 125 | 125 kHz |
| 1 或 250 | 250 kHz |
| 2 或 500 | 500 kHz |
| 3 或 7.8 | 7.8 kHz |
| 4 或 10.4 | 10.4 kHz |
| 5 或 15.63 | 15.63 kHz |
| 6 或 20.83 | 20.83 kHz |
| 7 或 31.25 | 31.25 kHz |
| 8 或 41.67 | 41.67 kHz |
| 9 或 62.5 | 62.5 kHz |

**示例**：

```text
AT+P2P=868000000:7:125:0:8:14
```

射频忙时返回 `AT_BUSY_ERROR`。

---

### 5.4 `AT+PRECV` — P2P 接收

| 项目 | 说明 |
|------|------|
| 功能 | 进入/退出 P2P 接收模式 |
| 查询 | `AT+PRECV=?` 或 `AT+PRECV` — 返回当前配置的时间参数 |
| 设置 | `AT+PRECV=<time>` |

**`time` 取值**：

| 值 | 含义 |
|----|------|
| `0` | 停止接收，切回可发送状态 |
| `1`–`65532` | 接收窗口时长（毫秒） |
| `65533` | 持续接收，仍允许 P2P 发送 |
| `65534` | 持续接收，需 `AT+PRECV=0` 退出 |
| `65535` | 收到一包后自动结束 |

**示例**：

```text
AT+PRECV=5000
AT+PRECV=65535
AT+PRECV=0
```

---

### 5.5 `AT+PSEND` — P2P 发送

| 项目 | 说明 |
|------|------|
| 功能 | 发送十六进制载荷 |
| 格式 | `AT+PSEND=<hex>` |
| 载荷 | 2–500 个十六进制字符（偶数长度），对应 **1–256 字节** |
| 示例 | `AT+PSEND=48656C6C6F`（"Hello"） |

仅支持 **SET** 形式。成功返回 `OK`，完成后可能有 `+EVT:TXP2P DONE`。

---

### 5.6 `AT+CW` — LoRa 连续波（CW）

| 项目 | 说明 |
|------|------|
| 功能 | 启动 SX1262 连续波发射 |
| 查询 | `AT+CW=?` — 返回当前 CW 参数 `freq:power:time_ms` |
| 设置 | `AT+CW=<freq>:<power>:<time_ms>` |

**参数**：

| 字段 | 范围 | 说明 |
|------|------|------|
| `freq` | 150000000–960000000 | 频率（Hz） |
| `power` | 5–22 | 发射功率（dBm） |
| `time_ms` | 0–65535 | 持续时间（ms）；**0** 表示持续发射直至其他射频操作打断 |

**示例**：

```text
AT+CW=868000000:14:0
AT+CW=915000000:20:10000
```

---

## 6. 低功耗

### 6.1 `AT+RTC` — System OFF 唤醒延时

| 项目 | 说明 |
|------|------|
| 功能 | 设置 `AT+SLEEP` 进入 `System OFF` 后的 GRTC 唤醒时间（秒） |
| 格式 | `AT+RTC=<wakeup_s>` |
| 查询 | `AT+RTC=?` |
| 关闭 | `AT+RTC=0`（禁用 RTC 唤醒） |

**示例**：

```text
AT+RTC=10
AT+RTC=?
AT+RTC=0
```

---

### 6.2 `AT+SLEEP` — 延时关机

| 项目 | 说明 |
|------|------|
| 功能 | LoRa 进入低功耗后，延时并 **System OFF** 关机 |
| 格式 | `AT+SLEEP=<delay_ms>[,<radio_mode>]` |
| `delay_ms` | 返回 `OK` 后等待的毫秒数（无上限校验，由调用方控制） |
| `radio_mode` | 可选，默认 `SLEEP`（warm sleep） |
| RTC 唤醒 | 若 `AT+RTC` 非 0，`AT+SLEEP` 关机前会配置 GRTC 定时唤醒 |

**`radio_mode` 可选值**：

| 值 | 说明 |
|----|------|
| `SLEEP` / `SLEEP_WARM` | Warm sleep（默认） |
| `SLEEP_COLD` | Cold sleep |
| `STDBY_RC` / `STANDBY_RC` | Standby RC |
| `STDBY_XOSC` / `STANDBY_XOSC` | Standby XOSC |

**示例**：

```text
AT+SLEEP=1000
AT+SLEEP=500,SLEEP_COLD
AT+RTC=5
AT+SLEEP=0
```

---

## 7. 外设与射频测试

### 7.1 `AT+TEST` — 硬件波形/功能测试

| 子命令 | 功能 |
|--------|------|
| `IIC` | I2C 波形：向地址 `0x50` 写数据，SCL/SDA = P0.03/P0.02，重复 100 次 |
| `SPI` | SPI00 波形：SCK P2.01，MOSI P2.02，MISO P2.04，CS P2.05，2 MHz |
| `UART` | UART21 发送测试字符串，TX P2.08 |
| `BLE` | 启动 BLE 不可连接广播，名称 `CONFIG_BT_DEVICE_NAME`（默认 `rak3162_hw_test`） |
| `GRTC` | 在 P2.06（丝印 GPIO4）输出约 **32.768 kHz** 方波（需 overlay 中 `lf_mon_out` 节点） |
| `GRTCSTOP` | 停止 GRTC 方波输出 |

**示例**：

```text
AT+TEST=IIC
AT+TEST=GRTC
AT+TEST=GRTCSTOP
```

仅支持 `AT+TEST=<子命令>` 形式；`AT+TEST?` 显示帮助。

---

### 7.2 `AT+BLECW` — BLE 单载波（2.4 GHz）

使用芯片 **RADIO** 外设输出未调制载波（非 BLE 协议栈发包）。

| 项目 | 说明 |
|------|------|
| 查询 | `AT+BLECW=?` — 运行状态 / 频率 / 信道 / 功率 |
| 启动 | `AT+BLECW=<ch>[,<pwr_dbm>]` |
| 停止 | `AT+BLECWSTOP` |

**参数**：

| 参数 | 范围 | 说明 |
|------|------|------|
| `ch` | 0–100 | 信道号，频率 = **2400 + ch** MHz |
| `pwr_dbm` | 可选 | 发射功率（dBm），省略时默认 **8**；支持 -40,-20,-16,-12,-8,-4,0,2~8 等 |

**示例**：

```text
AT+BLECW=40
AT+BLECW=40,0
AT+BLECW=?
AT+BLECWSTOP
```

已在运行时再 `AT+BLECW` 会提示先 `AT+BLECWSTOP`。

---

## 8. 典型使用流程示例

### 8.1 产测串口与版本

```text
AT
AT+VER=?
AT+BUILDTIME=?
AT+SN=RAK3162TEST00000001
AT+SN=?
```

### 8.2 晶振电容校准

```text
AT+HFXOCAP=15000
AT+LFXOCAP=16000
AT+HFXOCAP=?
AT+LFXOCAP=?
```

### 8.3 LoRa P2P 互通

```text
AT+P2P=868000000:7:125:0:8:14
AT+PRECV=65535
AT+PSEND=01020304
AT+PRECV=0
```

### 8.4 LoRa 定频 CW

```text
AT+CW=868000000:14:0
```

---

## 9. 构建与运行说明

```bash
west build -b rak3162/nrf54l15/cpuapp samples/hw_test --no-sysbuild
```

- 无 Shield 时：LoRa/BLE CW 等射频类指令可能返回 `AT_ERROR` 或 `-ENODEV`。  
- `AT+TEST=GRTC` 需要 `boards/rak3162_nrf54l15_cpuapp.overlay` 中的 `lf_mon_out` 引脚定义。

---

## 10. 命令速查表

| 命令 | 类型 | 持久化 |
|------|------|--------|
| `AT` / `AT?` / `ATZ` / `ATE0`/`ATE1` | 系统 | — |
| `AT+VER` / `AT+BUILDTIME` | 只读 | — |
| `AT+SN` | 读写 | 是 |
| `AT+HFXOCAP` / `AT+LFXOCAP` | 读写 | 是 |
| `AT+DEVEUI` / `AT+APPEUI` | 读写 | 是 |
| `AT+P2P` | 读写 | 否（RAM） |
| `AT+PRECV` / `AT+PSEND` / `AT+CW` | 动作/查询 | 否 |
| `AT+SLEEP` | 动作 | — |
| `AT+TEST` | 动作 | — |
| `AT+BLECW` / `AT+BLECWSTOP` | 动作/查询 | 否 |
