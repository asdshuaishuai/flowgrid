## 1. 安全修复（PoC 驱动）

- [x] 1.1 fragment.go：分片入组 FragTot 一致性校验 + 32 组配额 + 惰性过期（PoC：`index out of range [200] with length 2` 复现后修复）
- [x] 1.2 fragment.go Feed：入组前拷贝分片字节（cgo 调用方释放缓冲后的 use-after-free）
- [x] 1.3 frame.go：Encode/EncodeBytes/EncodeBytesWithHMAC 对 >255 载荷返回 ErrInvalidPayload（PoC：300 字节载荷头部写 44）
- [x] 1.4 sequence.go：首帧须等于 seq_init；重复/过期帧显式拒绝（原静默放行）
- [x] 1.5 heartbeat.go：RecordPong 三重校验（未来时间戳/超界 RTT/ping 关联）；Tick 闩锁 + 锁外回调
- [x] 1.6 cgo/bridge.go：fg_latency_record_pong 适配双返回值并处理拒绝；cBuffer/goString 加固（malloc 判 nil、GoString 判空）；句柄从 1 起

## 2. Wire format 修订（v1.1）

- [x] 2.1 HMAC 截断 4 → 8 字节（frame.go/framing.go/测试/规范 §8.3、§7.1、§7.2、附录 B 全链同步）
- [x] 2.2 CLIPBOARD 载荷内嵌分段（§5.3）：TotalSize/FragIdx/FragTot；上限修正 63240；废除 §7.3 帧外分片
- [x] 2.3 新增 §7.4 文件传输协议与 FILE_* 帧类型（0x21-0x24）
- [x] 2.4 移除 BLE 参数交换 PSK Hash（§3.1.3，wifidirect.go 同步；cgo 导出参数保留保 ABI）
- [x] 2.5 §10.3 序列号判定重写；§2.3 版本语义澄清；§12.2 错误码 0x15 更名、0x16 新增

## 3. 会话层（客户端只写 GUI 的对接面）

- [x] 3.1 session.go：FrameStream（seq/时钟/sink）
- [x] 3.2 clipboard.go：ClipboardSession（SendClipboard 分段 + HandleFrame 重组/去重/换代/超时，回调锁外）
- [x] 3.3 filetransfer.go：FileSender（OfferFile/Pump/进度/完成回调）+ FileReceiver（OnOffer/Accept/Reject/Abort，SHA-256 校验后交付；stale active 过期）

## 4. 仓库整备

- [x] 4.1 移除 kde/、deepin/ 客户端实现（git 历史 e8d205c/d1e71e4 可考）
- [x] 4.2 AGENTS.md §八 27/28、§九 全节按新现实重写；CLAUDE.md 概览同步
- [x] 4.3 本归档建立（取代归档 spec frame-integrity-hmac 的 4 字节决策）

## 验证

- [x] `cd protocol && go test -count=1 .` 全绿（110+ 用例，含全部回归测试）
- [x] `go vet .` 干净；修改文件 `gofmt -e` 语法通过
- [ ] cgo 子包在有 gcc 的 Linux 环境跑 `go test ./...`（本机 CGO_ENABLED=0 无法覆盖）
