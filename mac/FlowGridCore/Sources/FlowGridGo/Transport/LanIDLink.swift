import Foundation

/// UDP 多播薄适配器：持有 socket，周期发送签名通告，收包喂给 Listener，定时 Prune。
/// 纯 BSD socket（多播接收 Network.framework 支持有限）。
/// 协议逻辑全在 GoLanID（cgo），本类只做 I/O 管道。
final class LanIDLink: @unchecked Sendable {
    private let announcer: GoLanID.Announcer
    private let listener: GoLanID.Listener
    private let group = "239.255.42.99"
    private let port: UInt16 = 24801
    private let announcePeriod: TimeInterval = 2.0
    private let prunePeriod: TimeInterval = 10.0
    private let maxAgeMs = 10_000

    private var sendSocket: Int32 = -1
    private var recvSocket: Int32 = -1
    private var recvSource: DispatchSourceRead?
    private var announceTimer: DispatchSourceTimer?
    private var pruneTimer: DispatchSourceTimer?
    private let queue = DispatchQueue(label: "flowgrid.lanid")
    private var started = false

    /// 对端列表变化回调（主线程）。
    var onPeersChanged: (([GoLanID.Peer]) -> Void)?

    init(announcer: GoLanID.Announcer, listener: GoLanID.Listener) {
        self.announcer = announcer
        self.listener = listener
    }

    func start() {
        guard !started else { return }
        started = true
        setupSendSocket()
        setupRecvSocket()
        startTimers()
    }

    func stop() {
        guard started else { return }
        started = false
        announceTimer?.cancel()
        pruneTimer?.cancel()
        recvSource?.cancel()
        announceTimer = nil
        pruneTimer = nil
        recvSource = nil
        if sendSocket >= 0 { close(sendSocket); sendSocket = -1 }
        if recvSocket >= 0 { close(recvSocket); recvSocket = -1 }
    }

    deinit {
        if sendSocket >= 0 { close(sendSocket) }
        if recvSocket >= 0 { close(recvSocket) }
    }

    // MARK: - 发送

    private func setupSendSocket() {
        let fd = socket(AF_INET, SOCK_DGRAM, 0)
        guard fd >= 0 else { return }
        var ttl: Int32 = 1
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, socklen_t(MemoryLayout<Int32>.size))
        sendSocket = fd
    }

    private func sendAnnouncement() {
        guard sendSocket >= 0 else { return }
        guard let wire = try? announcer.announce() else { return }
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = in_port_t(port).bigEndian
        _ = group.withCString { cstr in
            inet_pton(AF_INET, cstr, &addr.sin_addr)
        }
        wire.withUnsafeBytes { raw in
            guard let base = raw.baseAddress else { return }
            withUnsafePointer(to: &addr) { ptr in
                ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                    sendto(sendSocket, base, wire.count, 0, sa, socklen_t(MemoryLayout<sockaddr_in>.size))
                }
            }
        }
    }

    // MARK: - 接收

    private func setupRecvSocket() {
        let fd = socket(AF_INET, SOCK_DGRAM, 0)
        guard fd >= 0 else { return }
        var reuse: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = in_port_t(port).bigEndian
        addr.sin_addr.s_addr = INADDR_ANY.bigEndian
        let bindResult: Int32 = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                bind(fd, sa, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindResult == 0 else {
            close(fd)
            return
        }

        var mreq = ip_mreq()
        _ = group.withCString { cstr in
            inet_pton(AF_INET, cstr, &mreq.imr_multiaddr)
        }
        mreq.imr_interface.s_addr = INADDR_ANY.bigEndian
        setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, socklen_t(MemoryLayout<ip_mreq>.size))

        recvSocket = fd
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self] in
            self?.receivePacket()
        }
        source.setCancelHandler {
            close(fd)
        }
        recvSource = source
        source.resume()
    }

    private func receivePacket() {
        guard recvSocket >= 0 else { return }
        var buf = [UInt8](repeating: 0, count: 65536)
        var from = sockaddr_in()
        var fromLen = socklen_t(MemoryLayout<sockaddr_in>.size)
        let n: Int = buf.withUnsafeMutableBufferPointer { b in
            withUnsafeMutablePointer(to: &from) { fromPtr in
                fromPtr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                    withUnsafeMutablePointer(to: &fromLen) { lenPtr in
                        recvfrom(recvSocket, b.baseAddress, b.count, 0, sa, lenPtr)
                    }
                }
            }
        }
        guard n > 0 else { return }
        let data = Data(buf[0..<n])
        // 源 IP：sin_addr.s_addr 已是网络序，小端机上 withUnsafeBytes 即得网络序字节
        let ipBytes = withUnsafeBytes(of: from.sin_addr.s_addr) { Data($0) }
        listener.feed(data, from: ipBytes)
        notifyPeers()
    }

    // MARK: - 定时器

    private func startTimers() {
        let announce = DispatchSource.makeTimerSource(queue: queue)
        announce.schedule(deadline: .now(), repeating: announcePeriod)
        announce.setEventHandler { [weak self] in
            self?.sendAnnouncement()
        }
        announceTimer = announce
        announce.resume()

        let prune = DispatchSource.makeTimerSource(queue: queue)
        prune.schedule(deadline: .now() + prunePeriod, repeating: prunePeriod)
        prune.setEventHandler { [weak self] in
            guard let self else { return }
            _ = self.listener.prune(maxAgeMs: self.maxAgeMs)
            self.notifyPeers()
        }
        pruneTimer = prune
        prune.resume()
    }

    private func notifyPeers() {
        let peers = listener.peers()
        DispatchQueue.main.async { [weak self] in
            self?.onPeersChanged?(peers)
        }
    }
}
