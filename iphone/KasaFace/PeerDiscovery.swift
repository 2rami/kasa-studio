import Foundation
import Darwin

struct NearbyReceiver: Identifiable, Equatable {
    let id: String
    let name: String
    let host: String
    let port: UInt16
    let code: String
}

final class PeerDiscovery: NSObject, NetServiceBrowserDelegate, NetServiceDelegate {
    var changed: (([NearbyReceiver]) -> Void)?
    private let browser = NetServiceBrowser()
    private var services: [NetService] = []
    private var receivers: [String: NearbyReceiver] = [:]

    override init() {
        super.init()
        browser.delegate = self
        browser.includesPeerToPeer = true
    }
    func start() { browser.searchForServices(ofType: "_kasaface._udp.", inDomain: "local.") }
    func netServiceBrowser(_ browser: NetServiceBrowser, didFind service: NetService, moreComing: Bool) {
        services.append(service)
        service.delegate = self
        service.includesPeerToPeer = true
        service.resolve(withTimeout: 5)
    }
    func netServiceBrowser(_ browser: NetServiceBrowser, didRemove service: NetService, moreComing: Bool) {
        services.removeAll { $0 == service }
        receivers.removeValue(forKey: service.name)
        publish()
    }
    func netServiceDidResolveAddress(_ sender: NetService) {
        update(sender)
        sender.startMonitoring()
    }
    func netService(_ sender: NetService, didUpdateTXTRecord data: Data) { update(sender) }
    private func update(_ service: NetService) {
        guard let host = service.addresses?.compactMap(Self.ipv4Address).first ?? service.hostName, let data = service.txtRecordData(), let port = UInt16(exactly: service.port), port >= 1024 else { return }
        let values = NetService.dictionary(fromTXTRecord: data)
        guard let codeData = values["code"], let code = String(data: codeData, encoding: .utf8), code.count == 8,
              let roomData = values["room"], let room = String(data: roomData, encoding: .utf8), room.count == 32 else { return }
        receivers[service.name] = NearbyReceiver(id: room, name: service.name, host: host, port: port, code: code)
        publish()
    }
    private func publish() { changed?(receivers.values.sorted { $0.name < $1.name }) }
    static func ipv4Address(_ data: Data) -> String? {
        data.withUnsafeBytes { bytes in
            guard bytes.count >= MemoryLayout<sockaddr_in>.size, let base = bytes.baseAddress, base.assumingMemoryBound(to: sockaddr.self).pointee.sa_family == AF_INET else { return nil }
            var address = base.loadUnaligned(as: sockaddr_in.self)
            var text = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
            guard inet_ntop(AF_INET, &address.sin_addr, &text, socklen_t(text.count)) != nil else { return nil }
            return String(cString: text)
        }
    }
}
