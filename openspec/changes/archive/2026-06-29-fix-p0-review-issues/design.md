## Context

FlowGrid KDE 客户端经过全量审查后，在三个维度发现 28 项 P0 阻塞问题：

1. **协议层（6项）**: BLE 发现解析错误数据源（读取 `manufacturer_data` 而非 `service_data`）、DTLS 无证书导致握手必败、HMAC 帧完整性完全缺失、DTLS 版本常量使用 TLS1_3 而非 DTLS1_3、修饰符重映射从未被调用、HAL 架构上不支持修饰符注入
2. **UI 层（19项）**: 所有子窗口硬编码数据、多处 `hide()` 未定义导致运行时崩溃、Loader 二次打开失效、信号响应未更新 UI
3. **业务逻辑层（3项）**: `main.rs` 配置初始化二次 panic、`DeviceManager` 重连阻塞 `select!` 循环、`near_link.rs` `transmute_copy` 获取 SSL 指针存在 UB

这些问题的共同根源是：审查阶段（对比 design/PROTOCOL.md 和实际代码）发现了实现与规范的系统性偏离。本次变更目标是一次性修复所有 P0 问题，使应用达到可用状态。

**约束**:
- 不修改协议规范本身（`design/PROTOCOL.md` 是权威来源），只修复实现层
- 保持现有 API 和 UI 结构不变，仅修复绑定和缺陷
- 最小改动原则：每个修复只修改导致问题的具体代码，不引入额外重构
- 所有 unsafe 代码必须添加 `SAFETY` 注释（AGENTS.md §27）

## Goals / Non-Goals

**Goals:**
- 修复 BLE 发现使其正确解析协议广播数据（`service_data`）
- 实现 DTLS 自签名证书 + TOFU 指纹信任模型
- 修复 DTLS 版本常量使用正确的 DTLS1_3
- 添加帧 HMAC 完整性验证（4B SHA-256 截断）
- 修复 `PlatformHal` 支持修饰符注入
- 修复 `inject_hid` 调用 `remap_modifiers()`
- 修复 `main.rs` 配置初始化移除 panic 路径
- 修复 `DeviceManager` 重连非阻塞
- 修复 `near_link.rs` SSL 指针安全获取
- 修复所有 QML 子窗口绑定后端数据
- 修复 QML 运行时错误（`hide()`、Loader 二次打开）
- 实现 QML 信号响应刷新 UI

**Non-Goals:**
- 不新增功能（如剪切板同步、Raw Ethernet 模式、0-RTT PSK 缓存）
- 不重构现有架构（如替换 std::sync::Mutex 为 tokio::sync::Mutex）
- 不修改协议规范（PROTOCOL.md 不动）
- 不修改 P1/P2 问题（如 mDNS 实现、MouseInterpolator 集成）

## Decisions

### 1. DTLS 证书：首次启动生成自签名 ECDSA P-256 证书
- **选择**: 自签名证书 + 首次配对指纹信任（TOFU）
- **理由**: 协议不要求 PKI 基础设施。自签名证书足够提供加密通道，TOFU 模型在首次连接时自动信任对端指纹，后续连接验证指纹。这与 SSH 的 known_hosts 模型一致，用户零配置。
- **替代方案**: 使用系统 CA 证书链 — 被弃选，因为用户无法控制对端设备的证书，且增加配置复杂度。

### 2. HMAC 密钥：从 DTLS 会话密钥派生
- **选择**: `HKDF-SHA256(dtls_session_key, salt="FlowGrid-v1-HMAC", info="frame-integrity")`
- **理由**: 复用 DTLS 已协商的密钥材料，避免额外密钥交换。HKDF 保证派生密钥的密码学安全。
- **替代方案**: 独立预共享密钥 — 被弃选，增加管理复杂度且无安全增益。

### 3. 修饰符注入：在 HAL trait 层添加 `modifiers` 参数
- **选择**: 修改 `PlatformHal` trait 的 `inject_key_down` 和 `inject_key_up` 签名，增加 `modifiers: u8`
- **理由**: 这是最小改动方案。修饰符是 HID 事件不可分割的一部分，trait 层必须支持。`linux_input.rs` 中通过 `EV_KEY` 依次注入修饰符键和主键码。
- **替代方案**: 在 DeviceManager 层拆分修饰符和键码为独立事件 — 被弃选，会增加事件数量且不符合 uinput 最佳实践。

### 4. DeviceManager 重连：拆分为独立 tokio::spawn task
- **选择**: 在 `on_tick` 检测到需要重连时，spawn 一个独立 task 执行退避和连接尝试，完成后通过 mpsc 命令发回结果
- **理由**: 避免在 `select!` 的 `on_tick` 分支中直接 `await sleep`，这会阻塞 Actor 处理其他命令和心跳。独立 task 保证 Actor 始终响应。
- **替代方案**: 为每个设备维护独立 `tokio::time::Interval` — 被弃选，改动更大且增加了状态管理复杂度。

### 5. QML 数据绑定：全部改为读取 watch 缓存
- **选择**: 所有子窗口初始化时调用 `backend.get_*()` 读取 watch 缓存，信号触发时刷新
- **理由**: 现有架构已使用 `tokio::sync::watch` 缓存通道，只需在 QML 侧正确绑定。无需引入新的数据模型。
- **替代方案**: 为每个子窗口创建独立的数据模型 — 被弃选，增加冗余且不符合现有架构。

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| DTLS 自签名证书被中间人替换（首次连接时无现有指纹） | 首次连接必须在可信网络环境完成；30 天指纹过期后重新验证 |
| HMAC 计算增加每帧 ~2-5µs 延迟 | 使用 SHA-256 硬件加速（x86 AES-NI/SHA 扩展）；仅在 NearLink 启用，DirectLink TLS 不启用 |
| 修饰符状态跟踪增加复杂度 | 在 `DeviceManager::inject_hid` 中维护每设备 `modifier_state` 字段，释放时与 KEY_UP 对齐 |
| QML 信号绑定可能导致频繁刷新 | 使用 `Connections` 的 `onDevicesChanged` 等信号仅在数据变化时触发，避免轮询 |
| 修复范围大，引入回归风险 | 每个修复附带单元测试；保持 `cargo test --workspace` 全部通过 |

## Migration Plan

1. **协议层修复优先**（DTLS、HMAC、BLE）：这些修复影响连接建立，必须最先完成
2. **HID 层修复次之**（修饰符、HAL）：影响核心功能
3. **运行时修复并行**（Actor 阻塞、配置 panic）：不影响其他模块
4. **UI 层修复最后**：依赖前三层的数据正确性
5. **测试验证**：每个修复后运行 `cargo test --workspace` 和 `cargo clippy --workspace`

**Rollback**: 每个修复独立提交，可单独回滚。使用 `git` 分支管理。

## Open Questions

1. **指纹持久化格式**: 使用 JSON (`trusted_peers.json`) 还是 YAML？与现有配置一致（JSON）。
2. **证书有效期**: 自签名证书有效期设为 1 年还是 10 年？选择 10 年，减少用户干预。
3. **修饰符 KEY_UP 时序**: 当 KEY_UP 到达时，是否同时释放所有修饰符？选择：仅释放该 KEY_UP 对应的修饰符（由 `modifiers` 位决定），不释放其他仍被按下的修饰符。
