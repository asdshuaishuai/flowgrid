# FlowGrid Protocol Specification v1.1

> **Canonical Reference** — 所有平台实现必须遵循本文档。
> 修订日期: 2026-10-16

> **v1.1 修订记录 (2026-10-16)**
> 1. CLIPBOARD 载荷改为内嵌分段 (Total Size / Frag Idx / Frag Tot, §5.3)——v1.0 的
>    "64KB 剪贴板" 与 UInt8 载荷上限在数学上不可共存，属规范级缺陷。
> 2. 废除 §7.3 帧外分片机制 (合法帧最大 267 字节，永远用不到)。
> 3. 帧完整性 HMAC 截断 4 → 8 字节 (§8.3)，4 字节截断可在线暴破。
> 4. 移除 BLE WiFi Direct 参数交换中的 PSK Hash (§3.1.3)——广播哈希前缀构成
>    PSK 离线字典攻击面。
> 5. 澄清 §10.3 序列号判定 (首帧须等于 seq_init；重复/过期帧必须丢弃)。
> 6. 修正 §6.4 码制标注与 §11.1 示例键码。
> 7. 新增 §7.4 文件传输协议 (FILE_OFFER/ACK/CHUNK/ABORT, ≤16MiB, SHA-256 校验)。
> 8. 剪贴板接收侧规则明确: Idx=0 换代、5s 超时、同刻单传输；错误码 0x15
>    更名为 transferTimeout。
> 9. 澄清 §2.3 版本语义（v1.0 线格式从未发布，版本 1 即本文档线格式）。
> 10. 客户端实现整体移除待重写；§12.2 新增 0x16 transferRejected。
> 11. 稳定性强化（线格式零变更）：§5.3 超时改为"5 秒无进展"（活动锚点）、
>     孤儿段静默丢弃、剪贴板支持整轮重发；§7.4 发送方自动重发轮次
>     （默认额外 2 轮，接收端按 ChunkIdx 去重幂等）、迟到 CHUNK 静默丢弃。

---

## 目录

1. [协议概述](#1-协议概述)
2. [术语与约定](#2-术语与约定)
3. [设备发现协议](#3-设备发现协议)
4. [配对与握手协议](#4-配对与握手协议)
5. [HID 帧格式](#5-hid-帧格式)
6. [键码与修饰符映射](#6-键码与修饰符映射)
7. [数据传输协议](#7-数据传输协议)
8. [加密与安全](#8-加密与安全)
9. [保活与延迟测量](#9-保活与延迟测量)
10. [重连与容错](#10-重连与容错)
11. [键位映射规则格式](#11-键位映射规则格式)
12. [错误码](#12-错误码)
13. [平台实现要求](#13-平台实现要求)

---

## 1. 协议概述

FlowGrid 使用两模型架构，两种模型共享相同的 HID 帧格式和键码体系：

| | 直连 (DirectLink) | 智能连接 (IntelligentLink) |
|---|---|---|
| **场景** | 有线/局域网键鼠共享 | 局域网去中心识别（无 BLE、mDNS 不可用时兜底） |
| **发现** | mDNS (Bonjour/Avahi) | UDP 多播通告 + 私有链识别 |
| **传输** | TCP | UDP（发现）+ TCP（数据）协同 |
| **加密** | TLS 1.3（可选）+ 帧 HMAC（§8.3） | 帧 HMAC（§8.3，复用直连会话） |
| **典型延迟** | < 0.5ms | 同直连（数据通道相同） |
| **重连** | 0s（子网记忆） | 同直连 |
| **路由依赖** | 无（直连/局域网） | 无（局域网） |

**设计哲学：** 受 AirDrop 启发——去中心服务做发现和识别，高速通道做数据。智能连接以局域网私有链（UDP 多播通告 + 密码学身份 + 链式信任，§3.1）实现去中心识别，不依赖 BLE 等射频硬件；数据经 TCP 传输（§7.2）。BLE 发现与 UDP+DTLS 传输（原 NearLink）为后续迭代预留。

---

## 2. 术语与约定

### 2.1 角色定义

| 角色 | 说明 |
|------|------|
| **Host** | 拥有物理键鼠的设备，发送 HID 事件 |
| **Target** | 被控制的远端设备，接收并注入 HID 事件 |
| **Peer** | 通信对端（Host 和 Target 互为 Peer） |

### 2.2 数据格式约定

- **字节序**: 所有协议字段使用 **大端序 (Network Byte Order)**，除非另有说明
- **对齐**: 结构体不填充，紧凑排列
- **长度字段**: 明确标注位宽 (UInt8/UInt16/UInt32/UInt64)
- **保留字段**: 必须置零，接收方必须忽略

### 2.3 版本号

```
Protocol Version:  1
Frame Version:     1
Key Map Version:   "1.0"
```

**版本语义（v1.1 澄清）**：Protocol Version `1` 唯一标识**本文档**定义的线格式。
v1.0 文档描述的线格式从未有对外发布的实现（客户端实现已于 2026-10 重置），
因此无需升号；BLE 广播（§3.1.2）与 IDENTIFY（§4.2）的版本检查点均以本字段为准。
未来任何不兼容的线格式变更必须升号或新增能力位。

---

## 3. 设备发现协议

### 3.1 智能连接 — LAN 私有链发现

#### 3.1.1 多播通告

设备在局域网多播组周期广播签名通告：

```
多播组:      239.255.42.99
端口:        24801 (与直连一致)
TTL:         1 (不出局域网)
通告周期:    2s

通告记录:
  Byte 0:      Version (UInt8, 0x01)
  Byte 1:      Platform (UInt8, 见 §3.1.4)
  Byte 2:      Caps (UInt8, 见 §3.1.4)
  Byte 3-4:    Port (UInt16 BE, 本机直连 TCP 监听端口)
  Byte 5:      NameLen (UInt8)
  Byte 6..6+N: Name (UTF-8, N ≤ 64)
  随后 65 字节: IdentityPubKey (SEC1 非压缩点, 编码同 §4.3)
  随后 64 字节: Signature (ECDSA-P256-SHA256, 覆盖除签名外全部字段)
```

监听方收通告后用通告内 IdentityPubKey 验签，通过即识别为候选对端（记录 name/platform/caps/端点/指纹/lastSeen）；10s 未更新自动淘汰；验签失败静默丢弃（网络噪声）。

#### 3.1.2 身份与验签

每设备持有持久身份密钥对（P-256）。指纹 = hex(SHA-256(IdentityPubKey)[0:16])（32 个十六进制字符），用于人工核对与信任持久化。

#### 3.1.3 私有链信任（局域网私有区块链）

每设备维护哈希链式签名信任日志，只追加：

```
信任记录:
  PeerID:     对端身份公钥 (65 字节)
  Fingerprint: 对端指纹 (16 字节, hex(SHA-256(PeerID)[0:16]))
  Decision:   0x00 = trust, 0x01 = reject
  Timestamp:  Unix 秒 (UInt64 BE)
  PrevHash:   前一条记录的 SHA-256 (32 字节, 首条为全零)
  Signature:  ECDSA-P256-SHA256(IdentityPrivKey, 除签名外全部字段) (64 字节)
```

性质：签名防伪、PrevHash 链式防篡改、只追加（撤销 = 追加 reject 记录而非改写）、去中心（每设备本地链，无需共识）。首接触：展示指纹待人工确认后追加 trust 记录；后续：验签 + 指纹命中链上最新 trust → 自动信任；不命中 → 提示确认，不自动连接。

#### 3.1.4 平台与能力字段

**Device Platform 字段:**

| 值 | 平台 |
|---|---|
| `0x01` | macOS |
| `0x02` | Windows |
| `0x03` | Linux |
| `0x04` | Android |
| `0x05` | iOS/iPadOS |

**Capability Flags:**

```
Bit 0 (0x01):  支持 IntelligentLink
Bit 1 (0x02):  支持 DirectLink
Bit 2 (0x04):  支持 WiFi Direct
Bit 3 (0x08):  支持 DTLS 1.3
Bit 4 (0x10):  支持 剪贴板同步
Bit 5 (0x20):  支持 文件传输
Bit 6-7:       保留 (置零)
```

> **BLE 发现（预留）**：原 NearLink 的 BLE 服务定义（0x1850）、广播数据、WiFi Direct 参数交换、信道避让与 RSSI 分级为后续迭代预留（BLE 射频路径），本文档暂不展开；协议库对应编解码（BLE 广播 / WiFi Direct 参数）已实现保留。

### 3.2 DirectLink — mDNS 发现

#### 3.2.1 mDNS Service 注册

```
Service Type:  _flowgrid._tcp
Domain:        local

TXT Records:
  ┌────────────────┬──────────────────────────────────────────┐
  │ Key            │ Value 示例/说明                           │
  ├────────────────┼──────────────────────────────────────────┤
  │ version        │ "1"                                      │
  │ platform       │ "macos" / "windows" / "linux"            │
  │ name           │ "MacBook Pro" (设备名称)                  │
  │ caps           │ "nearlink,directlink,dtls,clipboard"     │
  │ port           │ "24801" (UDP 数据端口)                    │
  │ encryption     │ "dtls13" / "none"                        │
  │ hwaddr         │ "a1:b2:c3:d4:e5:f6" (MAC 地址)          │
  └────────────────┴──────────────────────────────────────────┘
```

#### 3.2.2 APIPA 地址协商 (以太网直连)

用于无路由器的以太网直连场景 (点对点网线连接)：

```
分配规则:
  1. 随机生成第三字节 R ∈ [1, 254]
  2. Host:   169.254.{R}.1/30
  3. Target: 169.254.{R}.2/30
  4. 两个可用 IP 在同一 /30 子网

冲突检测:
  发送 ARP Probe → 无回复则使用 → 有回复则重新随机

持久化:
  成功连接后将子网信息持久化到本地
  下次连接直接使用记忆的子网 (0 秒重连)

注意: 仅创建虚拟网卡，不修改系统路由表
```

---

## 4. 配对与握手协议

### 4.1 配对流程

```
Host                                    Target
  │                                        │
  │──── BLE Scan / mDNS Browse ──────────→│  (发现)
  │←─── Advertisement / TXT Record ───────│
  │                                        │
  │──── WiFi Direct 参数请求 ─────────────→│  (BLE 交换)
  │←─── 参数确认 ─────────────────────────│
  │                                        │
  │════ WiFi Direct / TCP 连接建立 ══════→│  (连接)
  │                                        │
  │──── DTLS ClientHello ────────────────→│  (加密握手)
  │←─── DTLS ServerHello + Certificate ──│
  │──── DTLS Finished ───────────────────→│
  │←─── DTLS Finished ───────────────────│
  │                                        │
  │──── IDENTIFY (0x10) ────────────────→│  (身份交换)
  │←─── IDENTIFY_RESPONSE (0x11) ────────│
  │                                        │
  │═════ 双向 HID 帧流 (已加密) ══════════│  (运行)
  │                                        │
```

### 4.2 IDENTIFY 帧 (0x10)

加密握手成功后，双方交换身份信息：

```
IDENTIFY (0x10) Payload:
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬────────────┐
│ Byte 0   │ Byte 1   │ Byte 2   │ Byte 3   │ Byte 4-7 │ Byte 8+  │            │
│ Version  │ Platform │ Role     │ Caps     │ Seq Init │ Name     │            │
│ (UInt8)  │ (UInt8)  │ (UInt8)  │ (UInt8)  │ (UInt32) │ (UTF-8)  │            │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴────────────┘

Role:   0x01 = Host (键鼠发送方), 0x02 = Target (键鼠接收方), 0x03 = 双向
Seq Init: 起始序列号 (随机值，防止序列号冲突)
Name: UTF-8 编码的设备名称，以 \0 结尾
```

```
IDENTIFY_RESPONSE (0x11) Payload:
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ Byte 0   │ Byte 1   │ Byte 2   │ Byte 3+  │          │
│ Status   │ Platform │ Caps     │ Name     │          │
│ (UInt8)  │ (UInt8)  │ (UInt8)  │ (UTF-8)  │          │
└──────────┴──────────┴──────────┴──────────┘

Status: 0x00 = 接受, 0x01 = 版本不兼容, 0x02 = 拒绝, 0x03 = 需要用户确认
```

### 4.3 首次握手密钥交换

首次配对使用 ECDH P-256 交换会话密钥：

```
1. 双方生成临时 ECDH P-256 密钥对
2. 交换公钥 (通过 BLE 或 DTLS handshake)
3. 计算共享密钥: shared_secret = ECDH(local_private, remote_public)
4. 派生会话密钥: session_key = HKDF-SHA256(shared_secret, salt="FlowGrid-v1", info="session-key")
5. 持久化对端公钥指纹，后续连接自动信任
```

Wire 格式约定（所有平台一致）：

```
公钥编码:      SEC1 非压缩点, 65 字节 (0x04 || X || Y)
共享密钥:      ECDH 输出, 32 字节 (P-256 x 坐标)
会话密钥:      HKDF-SHA256 输出, 32 字节
公钥指纹:      hex(SHA-256(公钥 65 字节)[0:16]) — 32 个十六进制字符
```

---

## 5. HID 帧格式

### 5.1 帧头 (Header)

所有 HID 帧共享相同的头部结构：

```
 Offset   Size    Field           说明
──────────────────────────────────────────────────
 0        1       Frame Type      帧类型 (见 5.2)
 1        2       Sequence        序列号 (UInt16 BE), 从 IDENTIFY.seq_init 递增
 3        8       Timestamp       微秒时间戳 (UInt64 BE)
 11       1       Payload Length  载荷长度 N (UInt8, max 255)
 12       N       Payload         变长载荷 (见 5.3)
──────────────────────────────────────────────────

总帧长: 12 + N 字节
最小帧: 12 字节 (无载荷，用于心跳)
典型帧: 14-16 字节 (键鼠事件)
最大帧: 267 字节 (255 字节载荷)
```

### 5.2 帧类型定义

```
 Frame Type   Value   Direction         说明
──────────────────────────────────────────────────────────
 KEY_DOWN     0x01    Host → Target     按键按下
 KEY_UP       0x02    Host → Target     按键释放
 MOUSE_MOVE   0x03    Host → Target     鼠标移动
 MOUSE_BTN    0x04    Host → Target     鼠标按钮
 SCROLL       0x05    Host → Target     滚轮事件
 GESTURE      0x06    Host → Target     手势事件 (保留)
 CLIPBOARD    0x07    双向              剪贴板同步
 HEARTBEAT    0x08    双向              保活心跳
 LATENCY_PING 0x09    Host → Target     延迟测量请求
 LATENCY_PONG 0x0A    Target → Host     延迟测量响应
 IDENTIFY     0x10    双向              身份交换
 IDENTIFY_RES 0x11    双向              身份响应
 DISCONNECT   0x20    双向              主动断开
 FILE_OFFER   0x21    Sender→Receiver   文件传输请求 (§7.4)
 FILE_ACK     0x22    Receiver→Sender   接受/拒绝/完成确认 (§7.4)
 FILE_CHUNK   0x23    Sender→Receiver   文件数据块 (§7.4)
 FILE_ABORT   0x24    双向              取消传输 (§7.4)
 ERROR        0xFF    双向              错误通知
──────────────────────────────────────────────────────────
```

### 5.3 载荷结构

#### KEY_DOWN (0x01) — 3 bytes

```
Offset  Size   Field         说明
 0      2      Key Code      HID Usage ID (UInt16 BE, 见 §6)
 2      1      Modifiers     修饰键位掩码 (见 §6.2)
```

#### KEY_UP (0x02) — 2 bytes

```
Offset  Size   Field         说明
 0      2      Key Code      HID Usage ID (UInt16 BE)
```

#### MOUSE_MOVE (0x03) — 4 bytes

```
Offset  Size   Field         说明
 0      2      Delta X       相对 X 偏移 (Int16 BE, -32768 ~ 32767)
 2      2      Delta Y       相对 Y 偏移 (Int16 BE)
```

注: 支持高 DPI 显示器的高精度位移。鼠标插值在 Host 端完成后再发送。

#### MOUSE_BTN (0x04) — 2 bytes

```
Offset  Size   Field         说明
 0      1      Button ID     按钮编号 (见下表)
 1      1      State         0x00 = 释放, 0x01 = 按下
```

```
Button ID:
  0x01   Left
  0x02   Right
  0x03   Middle
  0x04   Button 4 (后退)
  0x05   Button 5 (前进)
```

#### SCROLL (0x05) — 4 bytes

```
Offset  Size   Field         说明
 0      2      Delta Y       垂直滚动量 (Int16 BE, 正=向上)
 2      2      Delta X       水平滚动量 (Int16 BE, 正=向左)
```

注: 滚动量单位为「行/格」（1 = 一格），不是像素。Windows `WM_MOUSEWHEEL` 的
±120 折算为 ±1；macOS 使用 line delta；Linux evdev `REL_WHEEL` 的值本来就是格。
像素级平滑滚动留给未来的能力位与独立字段，不复用本帧语义（见
docs/PROTOCOL-ISSUES.md P11）。这是 v1.1 的语义勘误：线格式与字段宽度未变，
只是把原文的「像素」纠正为各端实际实现的「格/行」。

#### CLIPBOARD (0x07) — 变长 (v1.1)

```
Offset  Size   Field         说明
 0      1      MIME Type Len MIME 类型长度 M
 1      M      MIME Type     UTF-8 编码 MIME 类型 (如 "text/plain")
 1+M    4      Total Size    剪贴板数据总长度 D (UInt32 BE)
 5+M    1      Frag Idx      分段索引 (UInt8, 0-based)
 6+M    1      Frag Tot      总分段数 (UInt8, ≥1)
 7+M    N      Data          本段数据 (N ≤ 255-(7+M))
```

单帧可载数据上限为 `255-(7+M)` 字节（MIME 为 "text/plain" 时约 238 字节）。
剪贴板总大小上限 = `255 × (248-M)` 字节：空 MIME 时全局上限 **63240 字节**，
"text/plain" 时 60690 字节。（v1.0 宣称的 64KB=65536 在 UInt8 分段数下不可达，
一并修正。）超过单帧上限的剪贴板按 Frag Idx / Frag Tot 分段：每段一个独立
CLIPBOARD 帧、按 Sequence 顺序发送；接收方按 Idx 收齐 Tot 段后按序拼接，
拼接总长必须等于 Total Size。

网络波动行为（接收方）：
- **重复段**：静默忽略（按 Idx 去重）
- **乱序段**：按 Idx 缓冲，到达顺序无关
- **丢段**：**5 秒无进展**（未收到任何新段）则丢弃整份；有进展则计时器重锚，
  慢而不断的传输不会被误杀。发送方可整轮重发修复丢段（接收方按 Idx 去重）
- **孤儿段**：无进行中传输时收到的非零 Idx 段（完成后或超时后的迟到副本）
  静默丢弃；只有 Idx=0 的段可以开启新一份传输。**因此发送端在可能乱序/丢段
  的传输上必须使用 ≥2 轮（SendClipboardPasses）**——载荷无传输 ID，单轮纯
  乱序无法与迟到副本区分
- **换代**：进行中传输收到 Idx=0 且形状（Tot/MIME/TotalSize）不同的段 → 替换
- 同一时刻只允许一个剪贴板传输（设备配对为 1:1）。

v1.0 的 "Data Length + 全量 Data" 单帧格式已废除：UInt8 载荷上限下单帧最多
携带约 250 字节，64KB 目标必须走分段。

#### HEARTBEAT (0x08) — 0 bytes

无载荷。收到后立即回复 HEARTBEAT。

#### LATENCY_PING (0x09) — 8 bytes

```
Offset  Size   Field         说明
 0      8      Timestamp     发送时的时间戳 (UInt64 BE, 微秒)
```

#### LATENCY_PONG (0x0A) — 8 bytes

```
Offset  Size   Field         说明
 0      8      Timestamp     原样返回 PING 中的时间戳
```

延迟计算: `latency_us = (now() - pong.timestamp) / 2`

#### DISCONNECT (0x20) — 1 byte

```
Offset  Size   Field         说明
 0      1      Reason        断开原因 (见 §12)
```

#### ERROR (0xFF) — 变长

```
Offset  Size   Field         说明
 0      1      Error Code    错误码 (见 §12)
 1      1      Message Len   错误消息长度 L
 2      L      Message       UTF-8 错误描述
```

---

## 6. 键码与修饰符映射

### 6.1 HID Usage ID 表

采用 USB HID Usage Table (Keyboard/Keypad Page 0x07) 标准：

```
 Key             Usage ID   Key             Usage ID
──────────────────────────────────────────────────────
 A               0x04       B               0x05
 C               0x06       D               0x07
 E               0x08       F               0x09
 G               0x0A       H               0x0B
 I               0x0C       J               0x0D
 K               0x0E       L               0x0F
 M               0x10       N               0x11
 O               0x12       P               0x13
 Q               0x14       R               0x15
 S               0x16       T               0x17
 U               0x18       V               0x19
 W               0x1A       X               0x1B
 Y               0x1C       Z               0x1D

 1               0x1E       2               0x1F
 3               0x20       4               0x21
 5               0x22       6               0x23
 7               0x24       8               0x25
 9               0x26       0               0x27

 Enter           0x28       Escape          0x29
 Backspace       0x2A       Tab             0x2B
 Space           0x2C       Caps Lock       0x39

 F1              0x3A       F2              0x3B
 F3              0x3C       F4              0x3D
 F5              0x3E       F6              0x3F
 F7              0x40       F8              0x41
 F9              0x42       F10             0x43
 F11             0x44       F12             0x45

 Print Screen    0x46       Scroll Lock     0x47
 Pause/Break     0x48       Insert          0x49
 Home            0x4A       Page Up         0x4B
 Delete          0x4C       End             0x4D
 Page Down       0x4E

 Right Arrow     0x4F       Left Arrow      0x50
 Down Arrow      0x51       Up Arrow        0x52

 Num Lock        0x53       Keypad /        0x54
 Keypad *        0x55       Keypad -        0x56
 Keypad +        0x57       Keypad Enter    0x58
 Keypad 1-9      0x59-0x61  Keypad 0        0x62
 Keypad .        0x63
```

### 6.2 修饰键 (Modifiers)

HID 帧中修饰键使用 **位掩码** 编码：

```
 Bit   Mask    Key
─────────────────────────────────────────────────
 0     0x01    Left Control
 1     0x02    Left Shift
 2     0x04    Left Alt (Option on macOS)
 3     0x08    Left Meta (Cmd on macOS, Win on Windows, Super on Linux)
 4     0x10    Right Control
 5     0x20    Right Shift
 6     0x40    Right Alt (AltGr on Windows)
 7     0x80    Right Meta
─────────────────────────────────────────────────

示例:
  Ctrl+C     →  Key Code: 0x06 (C), Modifiers: 0x01
  Cmd+V      →  Key Code: 0x19 (V), Modifiers: 0x08
  Shift+Tab  →  Key Code: 0x2B (Tab), Modifiers: 0x02
```

### 6.3 平台修饰键语义映射

不同平台的 Meta 键语义不同，需要自动映射：

```
 方向                映射规则
──────────────────────────────────────────────────────────────
 macOS → Windows     Cmd (0x08) → Ctrl (0x01)
 macOS → Linux       Cmd (0x08) → Ctrl (0x01)
 Windows → macOS     Ctrl (0x01) → Cmd (0x08)
 Linux → macOS       Ctrl (0x01) → Cmd (0x08)
 Windows ↔ Linux     无需映射
──────────────────────────────────────────────────────────────

即: macOS 的 Cmd 和 Windows/Linux 的 Ctrl 语义等价，互相对换。
其他修饰键 (Shift, Alt) 保持不变直通。
```

### 6.4 平台特定键码

```
注意: 本表各列码制不同，仅供 KeyMapper 层查表参考:
  macOS 列   = macOS 虚拟键码 (NSEvent keyCode)
  Windows 列 = Windows/HID 混合标注 (LCtrl 0xE0 为 HID Usage ID)
  Linux 列   = evdev 键码
HID 帧内一律使用标准 USB HID Usage ID，修饰键统一用 §6.2 位掩码。

 Key             macOS          Windows        Linux (X11/evdev)
──────────────────────────────────────────────────────────────
 Command/Win     0x37 (Cmd)     0xE3 (LWin)    0x85 (Super)
 Option/Alt      0x3A (LOption) 0xE2 (LMenu)   0x64 (Alt)
 Control         0x3B (LCtrl)   0xE0 (LCtrl)   0x25 (Ctrl)
 Fn              0x3F           N/A            N/A
```

映射在 KeyMapper 层完成，HID 帧始终使用标准 USB HID Usage ID。

---

## 7. 数据传输协议

### 7.1 IntelligentLink — UDP + DTLS（预留）

> **预留说明**：本节 UDP+DTLS 传输为后续 BLE 射频路径预留。当前智能连接（§3.1 LAN 私有链发现）的数据通道走 TCP（§7.2），UDP 仅用于多播发现通告。

```
传输层:
  Protocol:   UDP
  Port:       24801 (默认, 可配置)
  Encryption: DTLS 1.3
  Cipher:     AES-256-GCM
  MTU:        1400 bytes (WiFi Direct 安全值)

帧封装:
  每个UDP包 = 1个HID帧 (不合并, 避免延迟)
  若单帧 > MTU, 使用分片 (见 7.3)
```

**为什么选择 UDP 而非 TCP：**
- HID 帧仅 8-16 字节，TCP 的 Nagle 算法会累积 40-120ms
- DTLS 自带丢包检测和重传，无需 TCP 层
- UDP 无队头阻塞，单帧丢失不影响后续帧
- 99.9% 的帧在 3.25ms 内送达

### 7.2 DirectLink — TCP 或 Raw Ethernet

#### TCP 模式

```
传输层:
  Protocol:   TCP
  Port:       24801
  Encryption: TLS 1.3
  Keep-Alive: 启用, 间隔 2s

帧封装:
  使用 Length-Prefix Framing:
  ┌───────────────────────┬───────────────────┐
  │ Length (2B) UInt16 BE │ HID Frame (12+N)  │
  └───────────────────────┴───────────────────┘

  Length = HID 帧总长度 (含 header)
  启用帧 HMAC 时 (§8.3, 建议启用), Length 覆盖 帧体 + 8 字节 HMAC 尾。
  一个 TCP 包可包含多个帧 (批量发送)
```

#### Raw Ethernet 模式 (超低延迟)

```
用于以太网直连场景:
  EtherType:  0xF1A0 (FlowGrid 自定义)
  帧格式:
  ┌──────────────┬──────────┬──────────────┬──────────┬──────────┐
  │ Dest MAC (6B)│Src MAC(6B)│ EtherType(2B)│ HID Frame│ HMAC(8B) │
  │              │          │ 0xF1A0       │          │ 可选      │
  └──────────────┴──────────┴──────────────┴──────────┴──────────┘

  总帧长: 14 + (12+N) + 8 = 34+N 字节
  典型: 48-50 字节

  HMAC: HMAC-SHA256(session_key, frame) 的前 8 字节 (§8.3)
  物理安全场景可省略 HMAC

平台要求:
  Linux:   AF_PACKET/SOCK_RAW, 需 CAP_NET_RAW
  macOS:   Npcap/WinPcap (用户态)
  Windows: Npcap/WinPcap (用户态)
```

### 7.3 帧分片（v1.1 起废除，由 §5.3 载荷内分段取代）

v1.0 曾定义"在标准 HID 帧前添加 4 字节分片头 (Frag Seq/Idx/Tot)"的机制。
该机制与 §5.1 的 UInt8 载荷上限矛盾：合法帧最大 267 字节，在任何现实 MTU
（IntelligentLink 安全值 1400）下都不会超限，该机制从未有合法触发路径；而真正需要
分段的 64KB 剪贴板，正确做法是对 CLIPBOARD 载荷本身分段（见 §5.3）。

v1.1 起不再使用本机制。若未来版本提升载荷上限导致单帧可能超过 MTU，须重新
设计分片并将其纳入本节，且帧头必须携带可辨识标志。

实现层说明：`protocol/fragment.go` 保留了一套已加固的帧分片实现（FragTot
一致性校验、并发组配额），仅作为未来帧格式扩展的储备，当前任何路径都不使用；
对应 cgo 导出一并保留以稳定 C ABI。

---

### 7.4 文件传输协议 (v1.1 新增)

方向与能力：发送方必须置 CapabilityFlags.Bit5 (0x20, 文件传输)。每个方向
同一时刻只有一个传输；发现/配对层保证 1:1 配对。

目标场景：文档与截图等小文件 (≤ 16 MiB)。UInt8 载荷上限决定单帧数据块
≤ 249 字节，16 MiB 约需 6.7 万帧——更大媒体需要未来加宽长度字段的帧格式。

#### 帧与载荷

```
FILE_OFFER (0x21) — Sender → Receiver, 发起传输:
 0    2    TransferID  UInt16 BE (发送方生成, 方向内唯一, 非 0)
 2    8    Size        UInt64 BE (1 .. 16 MiB)
10    32   SHA-256     整个文件的哈希
42    1    NameLen M   文件名长度 (UTF-8, 仅文件名, 禁止路径分隔符)
43    M    Name

FILE_ACK (0x22) — Receiver → Sender:
 0    2    TransferID
 2    1    Status:  0x00 接受, 0x01 拒绝(忙), 0x02 拒绝(用户拒绝),
                   0x03 拒绝(超限), 0x04 完成但 SHA-256 不符, 0x05 完成(校验通过)

FILE_CHUNK (0x23) — Sender → Receiver:
 0    2    TransferID
 2    4    ChunkIdx    UInt32 BE (0-based)
 6    N    Data        N ≤ 249 (= 255 - 6)

FILE_ABORT (0x24) — 双向, 任意时刻取消:
 0    2    TransferID
 2    1    Reason:     0x00 用户取消, 0x01 错误, 0xFF 未知
```

#### 状态机

```
Sender:   OFFER(发) → [等 ACK ≤ 30s, 超时作废]
            ├─ ACK accepted → CHUNK 流式发送 (Pump) → [等 ACK complete,
            │    接收方 10s 无进展 → ABORT]
            ├─ ACK rejected → 结束(带原因)
            └─ ABORT → 结束
Receiver: OFFER(收) → 提示用户 (30s 未应答作废)
            ├─ Accept → ACK accepted → 缓存 CHUNK (10s 无进展失败)
            │    → 收齐后校验 SHA-256 → ACK complete (不符则 0x04)
            ├─ Reject → ACK rejected*
            └─ ABORT(收) → 结束
```

规则：
- TransferID 不匹配的帧一律丢弃（普通网络噪声，静默）；重复 CHUNK 按 ChunkIdx 去重，静默忽略。
- 数据在接收端校验通过前不得落盘到目标位置（先内存/临时文件）。
- HASH 不符不是传输层错误：以 FILE_ACK 0x04 上报，由用户决定重试。
- 流控由传输层承担（DTLS/TLS 可靠有序）；IntelligentLink UDP 上建议 Pump 节奏
  ≤ 5000 帧/秒, 避免突发挤占 HID 帧。
- **丢段恢复**：发送方 Pump 在一轮发完后自动从头重发，默认额外重发 2 轮
  （可配置，实现侧 SetResendPasses）；接收方按 ChunkIdx 去重，重发幂等。
  重发预算耗尽仍未完成 → 由 10s 无进展超时收敛为 transferTimeout。
- **迟到/未知 CHUNK**（传输已完成或超时后到达）：静默丢弃，不算错误。

---

## 8. 加密与安全

### 8.1 DTLS 1.3 握手

```
Client (Host)                           Server (Target)
    │                                        │
    │──── ClientHello ─────────────────────→│
    │     Supported Versions: DTLS 1.3      │
    │     Cipher Suites:                    │
    │       TLS_AES_256_GCM_SHA384          │
    │       TLS_CHACHA20_POLY1305_SHA256    │
    │     Key Share: ECDHE P-256            │
    │                                        │
    │←─── ServerHello ──────────────────────│
    │     Cipher: TLS_AES_256_GCM_SHA384    │
    │     Key Share: ECDHE P-256            │
    │                                        │
    │←─── Certificate + CertVerify ─────────│
    │     (自签名证书, 首次配对时信任指纹)     │
    │                                        │
    │──── Finished ────────────────────────→│
    │←─── Finished ────────────────────────│
    │                                        │
    │════ Application Data (加密) ══════════│
    │                                        │
```

### 8.2 0-RTT 会话恢复

用于快速重连：

```
1. 首次连接成功后，双方缓存 PSK (Pre-Shared Key) 和 session ticket
2. 重连时 Host 在 ClientHello 中携带 PSK + early_data
3. Target 验证 PSK 后直接处理 early_data
4. 省去完整握手，重连 < 200ms

安全限制:
  - early_data 仅用于 HEARTBEAT 和 LATENCY_PING (幂等帧)
  - HID 事件帧在握手完成后发送
  - PSK 有效期 7 天，过期需要重新握手
```

### 8.3 帧完整性验证

```
每帧可选 8 字节 HMAC:
  hmac = HMAC-SHA256(session_key, 完整帧字节)[0:8]
       = HMAC-SHA256(session_key, frame_type || seq || timestamp || payload_len || payload)[0:8]

  即: 使用标准 HMAC-SHA256 构造 (非 plain SHA-256)，
      输入为完整的 12+N 字节帧 (header + payload)。
      截断 8 字节 = 64-bit MAC: 在线伪造需 ~2^64 次, 不可行。
      (v1.0 的 4 字节截断在 1M pps 下约 1 小时可伪造一帧, 已废弃。)

验证流程:
  1. 接收帧后计算 HMAC
  2. 比较帧尾 8 字节
  3. 不匹配或缺失 → 丢弃帧 + 发送 ERROR (0xFF, code=0x10)
  4. 已启用 HMAC 的连接上，缺失 HMAC 尾的帧视为验证失败，不得静默放行
  5. HMAC 连续验证失败 ≥ 3 次 → 立即断开连接 (防暴破)

何时启用:
  - IntelligentLink (WiFi): 必须启用
  - DirectLink (TCP): 建议启用
  - DirectLink (Raw Ethernet, 物理隔离): 可选
```

---

## 9. 保活与延迟测量

### 9.1 心跳机制

```
发送方:    每 2 秒发送 HEARTBEAT (0x08) 帧
接收方:    收到后立即回复 HEARTBEAT (0x08)

超时判定:
  - 3 次心跳未回复 (6 秒) → 连接丢失
  - 触发重连流程 (见 §10)

状态更新:
  心跳成功 → 更新 AppState.connectionState = .connected
  心跳超时 → 更新 AppState.connectionState = .reconnecting(attempt: N)
```

### 9.2 延迟测量

```
Host 端:
  每 2 秒发送 LATENCY_PING (0x09)，携带当前时间戳 T1

Target 端:
  收到 LATENCY_PING 后立即回复 LATENCY_PONG (0x0A)
  原样返回 T1

Host 端:
  收到 LATENCY_PONG 后计算:
    rtt_us = now() - T1
    latency_us = rtt_us / 2

  采样记录 LatencySample:
    ┌──────────────┬──────────────┬──────────┐
    │ timestamp    │ value_ms     │ jitter   │
    │ Date         │ Double       │ Double   │
    └──────────────┴──────────────┴──────────┘

    jitter = |current_latency - previous_latency|

延迟质量评级:
  Good (< 2ms):   绿色 #10a36f
  Warn  (2-4ms):  琥珀色 #d97706
  Bad   (> 4ms):  红色 #dc2626

告警:
  连续 3 次 > 5ms → 触发 UI 告警横幅
```

---

## 10. 重连与容错

### 10.1 重连状态机

```
connected ──(心跳超时)──→ reconnecting
reconnecting ──(重连成功)──→ connected
reconnecting ──(重连失败)──→ reconnecting (attempt++)
reconnecting ──(超过最大次数)──→ idle

重连参数:
  最大重连次数:     5
  初始退避:         200ms
  退避倍增:         ×2 (200ms → 400ms → 800ms → 1.6s → 3.2s)
  最大退避:         5s
  抖动因子:         ±20%
```

### 10.2 重连流程

```
1. 检查 PSK 缓存
   ├─ 有 PSK → 0-RTT 恢复 (在 ClientHello 中携带 early_data)
   └─ 无 PSK → 完整 DTLS 握手

2. IntelligentLink 重连:
   → 尝试 WiFi Direct 重新连接 (不重新扫描 BLE)
   → 如果 WiFi Direct 失败 → 重新 BLE 扫描 + 握手

3. DirectLink 重连:
   → 使用缓存的 APIPA 子网 (0 秒重连)
   → 如果失败 → 重新 mDNS 发现

4. 重连成功后:
   → 发送 IDENTIFY 确认身份
   → 恢复 HID 帧流
   → 重置序列号 (使用新的 seq_init)
```

### 10.3 序列号处理

```
规则:
  - 序列号从 IDENTIFY.seq_init 开始递增
  - UInt16 范围: 0-65535，到达 65535 后回到 0
  - 接收方按 UInt16 回绕差值 diff = (received - expected) mod 65536 判定:
      diff == 0            → 符合预期, expected++
      0 < diff < 0x8000    → 超前 gap (丢包): 报 ERROR (code=0x20, "seq_gap"),
                             expected 推进到 received+1
      diff >= 0x8000       → 重复/过期帧 (含重放): 必须静默丢弃, 不得注入,
                             也不回 ERROR (防止错误风暴被利用为放大攻击)
  - 首帧必须等于 seq_init, 不符按 gap 处理并重新同步

重连:
  - 重连后重新交换 seq_init
  - 不沿用旧连接的序列号空间

防重放说明:
  本机制用于丢包检测与重放丢弃, 不替代传输层安全。IntelligentLink 的 DTLS 1.3
  自带记录层重放窗口; 未启用 DTLS 的链路 (Raw Ethernet) 上, 本机制 + 帧
  HMAC 是唯一的重放防线, 二者都必须启用。
```

---

## 11. 键位映射规则格式

### 11.1 YAML 规则文件

```yaml
# ~/Library/Application Support/FlowGrid/keymap.yaml (macOS)
# %APPDATA%/FlowGrid/keymap.yaml (Windows)
# ~/.config/flowgrid/keymap.yaml (Linux)

version: "1.0"
rules:
  # 基本键映射
  - fromOS: macOS
    toOS: windows
    fromKey: 0x06    # C (HID Usage ID, 见 §6.1)
    toKey: 0x06      # C
    modifiers: 8     # Cmd → Ctrl (0x08 → 0x01 在运行时转换)
    context: global

  # 应用特定映射
  - fromOS: macOS
    toOS: windows
    fromKey: 0x2B    # Tab
    toKey: 0x2B      # Tab
    modifiers: 2     # Shift
    context: app:com.microsoft.VSCode

  # 游戏配置
  - fromOS: windows
    toOS: macOS
    fromKey: 0x14    # Q
    toKey: 0x14      # Q
    modifiers: 0
    context: game
```

### 11.2 Context 格式

```
Context 值:
  global           — 全局映射 (默认)
  app:{bundle_id}  — 应用特定 (macOS: com.apple.Safari, Windows: vscode.exe)
  game             — 游戏模式 (禁用修饰键映射)
```

### 11.3 热重载

```
规则文件变更时自动重新加载，无需重启应用:
  1. 监听文件变更事件 (FSEvents / ReadDirectoryChangesW / inotify)
  2. 读取并解析新规则
  3. 验证语法
  4. 原子替换内存中的规则表
  5. 下一个 HID 帧开始使用新规则
```

---

## 12. 错误码

### 12.1 传输层错误

```
 Code   Name                    说明
──────────────────────────────────────────────────
 0x01   peripheralNotFound      BLE 外设未找到
 0x02   connectionFailed        连接失败
 0x03   connectionCancelled     连接被取消
 0x04   connectionTimeout       连接超时
 0x05   notConnected            未连接状态下的操作
 0x06   sendFailed              发送失败
 0x07   dtlsError               DTLS 握手/加密错误
 0x08   bluetoothNotAvailable   蓝牙不可用
 0x09   bluetoothPoweredOff     蓝牙未开启
 0x0A   bluetoothUnauthorized   蓝牙权限未授权
 0x0B   bluetoothNotSupported   设备不支持蓝牙
 0x0C   wifiDirectFailed        WiFi Direct 连接失败
 0x0D   mdnsError               mDNS 发现/注册错误
 0x0E   networkUnavailable      网络不可用
──────────────────────────────────────────────────
```

### 12.2 协议层错误

```
 Code   Name                    说明
──────────────────────────────────────────────────
 0x10   hmacMismatch            帧完整性验证失败
 0x11   unknownFrameType        未知帧类型
 0x12   invalidPayload          载荷格式错误
 0x13   versionMismatch         协议版本不兼容
 0x14   identifyRejected        身份验证被拒绝
 0x15   transferTimeout         剪贴板/文件传输超时 (v1.1 由 fragmentationTimeout 更名)
 0x16   transferRejected        传输被拒绝或中止 (v1.1 新增；本端错误值，不上线)
 0x20   sequenceGap             序列号间隙 (丢包)
──────────────────────────────────────────────────
```

### 12.3 HAL 错误

```
 Code   Name                    说明
──────────────────────────────────────────────────
 0x30   accessibilityDenied     辅助功能权限未授予
 0x31   eventCreationFailed     系统事件创建失败
 0x32   unsupportedEventType    不支持的事件类型
 0x33   platformNotSupported    不支持的平台
 0x34   uinputPermissionDenied  uinput 设备权限不足 (Linux)
──────────────────────────────────────────────────
```

### 12.4 断开原因 (DISCONNECT 帧)

```
 Code   Name                    说明
──────────────────────────────────────────────────
 0x00   userInitiated           用户主动断开
 0x01   shutdown                应用关闭
 0x02   switchDevice            切换到其他设备
 0x03   protocolError           协议错误
 0x04   securityError           安全验证失败
 0xFF   unknown                 未知原因
──────────────────────────────────────────────────
```

---

## 13. 平台实现要求

### 13.1 必须实现

每个平台实现必须提供：

| 能力 | 说明 |
|------|------|
| BLE 扫描 | 作为 Central 扫描 0x1850 Service |
| BLE 广播 | 作为 Peripheral 广播 0x1850 Service |
| WiFi Direct | 建立 P2P 连接 (Host 或 Client) |
| UDP 收发 | 端口 24801, 支持大端序帧解析 |
| DTLS 1.3 | AES-256-GCM 加密, 0-RTT 恢复 |
| HID 注入 | 键盘/鼠标事件注入到本地系统 |
| 心跳保活 | 2 秒间隔, 3 次超时断开 |
| 延迟测量 | Ping/Pong 机制 |
| 键位映射 | YAML 规则加载 + 热重载 |
| 配置持久化 | 设备信息 + 用户设置 + 会话密钥 |

### 13.2 平台特定要求

#### macOS

```
- Accessibility 权限: AXIsProcessTrusted()
- HID 注入: CGEvent(CGEventTap)
- 前台应用检测: NSWorkspace.shared.frontmostApplication?.bundleIdentifier
- BLE: CoreBluetooth (CBCentralManager / CBPeripheralManager)
- WiFi Direct: Network.framework (NWConnection)
- mDNS: Network.framework (NWBrowser / NWListener)
- 配置路径: ~/Library/Application Support/FlowGrid/
- 文件监听: DispatchSource.makeFileSystemObjectSource
```

#### Windows

```
- HID 注入: SendInput() / Input Simulator
- 前台应用检测: GetForegroundWindow() + GetWindowThreadProcessId()
- BLE: Windows.Devices.Bluetooth.Advertisement
- WiFi Direct: Wi-Fi Direct API (WFD)
- mDNS: DnsServiceBrowse() / DnsServiceRegister()
- 防火墙: 自动添加 UDP 24801 入站规则
- 配置路径: %APPDATA%/FlowGrid/
- 文件监听: ReadDirectoryChangesW
```

#### Linux (KDE)

```
- HID 注入: uinput (/dev/uinput, 需要 root 或 udev 规则)
- 前台应用检测: xdotool / KWindowSystem::activeWindow()
- BLE: Qt Bluetooth (QBluetoothDeviceDiscoveryAgent)
- WiFi Direct: wpa_supplicant / NetworkManager
- mDNS: KDNSSD / Avahi
- D-Bus: 注册 org.flowgrid 服务
- 配置路径: ~/.config/flowgrid/
- 文件监听: inotify (QFileSystemWatcher)
- Raw Ethernet: AF_PACKET/SOCK_RAW, 需 CAP_NET_RAW
```

### 13.3 兼容性矩阵

```
                    macOS Host    Windows Host    Linux Host
  macOS Target       ✅ NL/DL      ✅ NL/DL        ✅ NL/DL
  Windows Target     ✅ NL/DL      ✅ NL/DL        ✅ NL/DL
  Linux Target       ✅ NL/DL      ✅ NL/DL        ✅ NL/DL

  NL = IntelligentLink (BLE + WiFi Direct + UDP/DTLS)
  DL = DirectLink (mDNS + TCP/TLS 或 Raw Ethernet)

  任意两个平台的任意组合均支持双向通信。
```

---

## 附录 A: 默认端口

| 端口 | 协议 | 用途 |
|------|------|------|
| 24801/UDP | IntelligentLink | HID 数据传输 + DTLS |
| 24801/TCP | DirectLink | HID 数据传输 + TLS |
| - | BLE | 发现 + 参数交换 (无固定端口) |
| - | mDNS | 设备发现 (_flowgrid._tcp) |

## 附录 B: 帧校验和 (可选)

当 DTLS 未启用时 (如物理隔离的 Raw Ethernet 模式)，使用 CRC32 校验：

```
多项式: CRC-32/IEEE 802.3 (即 zlib / 以太网 FCS 使用的标准 CRC32)
计算: CRC32(完整 12+N 帧字节) = CRC32(frame_type || seq || timestamp || payload_len || payload)
存储: 4 字节, 大端序
接收方验证失败 → 丢弃帧 + ERROR (0xFF, code=0x10)

Raw Ethernet 帧中 CRC 与 HMAC 共存时的布局:
  ... │ HID Frame (12+N) │ CRC (4B) │ HMAC (8B) │
  两者均只覆盖 12+N 帧字节，互不嵌套。
```

## 附录 C: 鼠标插值参数 (Host 端)

```
插值在 Host 端完成，插值后的鼠标位移通过 MOUSE_MOVE 帧发送。

参数:
  历史样本数:    5
  抖动阈值:      2.0 px (低于此值的移动被忽略)
  平滑因子:      0.3 (0=不平滑, 1=完全平滑)
  预测帧间隔:    16.67ms (60fps)
  最大插值距离:  5.0 px (中间点生成)

算法:
  1. 记录最近 N 个鼠标位置 + 时间戳
  2. 计算速度向量: v = Δpos / Δt
  3. 预测下一帧位置: predicted = last_pos + v × frame_time
  4. 生成中间点: 如果 Δ > max_distance, 插入插值点
  5. 发送平滑后的 Δx, Δy
```

## 附录 D: 协议版本协商

```
未来版本升级时的兼容性保证:

1. IDENTIFY 帧携带 Protocol Version
2. 双方取最小公共版本号
3. 新版本帧类型使用 > 0x20 的值
4. 未知帧类型必须静默忽略 (不报错)
5. 新增的载荷字段追加到末尾，旧实现读取已知长度后截断
```
