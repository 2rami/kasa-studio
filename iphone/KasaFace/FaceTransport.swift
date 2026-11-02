import Foundation
import Network

final class FaceTransport {
    var status: ((Bool, String) -> Void)?
    var paired: ((URL) -> Void)?
    private let queue: DispatchQueue
    private let relayURL: URL
    private var active = false
    private var receiver = ReceiverPairing()
    private var session = ""
    private var nonce = ""
    private var udp: NWConnection?
    private var websocket: URLSessionWebSocketTask?
    private var relayReady = false
    private var sending = false
    private var reconnectAt: TimeInterval = 0
    private var timer: DispatchSourceTimer?
    private var localAck: TimeInterval = -.infinity
    private var remoteAck: TimeInterval = -.infinity
    private var sequence: UInt64 = 0

    init(queue: DispatchQueue, relayURL: URL = ReceiverPairing.relay) { self.queue = queue; self.relayURL = relayURL }

    func start(receiver: ReceiverPairing, session: String) {
        queue.async {
            self.active = true
            self.receiver = receiver
            self.session = session
            self.nonce = UUID().uuidString.replacingOccurrences(of: "-", with: "").lowercased()
            self.localAck = -.infinity
            self.remoteAck = -.infinity
            self.sequence = 0
            self.reconnectAt = 0
            self.openDirect()
            self.openRelay()
            let timer = DispatchSource.makeTimerSource(queue: self.queue)
            timer.schedule(deadline: .now(), repeating: .milliseconds(500))
            timer.setEventHandler { [weak self] in self?.tick() }
            self.timer = timer
            timer.resume()
        }
    }

    func addDirect(_ receiver: NearbyReceiver) {
        queue.async {
            guard self.active, self.receiver.room == receiver.id else { return }
            self.receiver.host = receiver.host
            self.receiver.port = receiver.port
            self.receiver.code = receiver.code
            self.openDirect()
        }
    }

    private func openDirect() {
        udp?.cancel()
        guard !receiver.host.isEmpty else { return }
        let connection = NWConnection(host: NWEndpoint.Host(receiver.host), port: NWEndpoint.Port(rawValue: receiver.port)!, using: .udp)
        udp = connection
        connection.start(queue: queue)
        receiveDirect(connection)
    }

    private func receiveDirect(_ connection: NWConnection) {
        connection.receiveMessage { [weak self] data, _, _, error in
            guard let self, self.active, self.udp === connection else { return }
            if let data, let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                if value["type"] as? String == "ack", value["session"] as? String == self.session {
                    self.localAck = ProcessInfo.processInfo.systemUptime
                } else if value["type"] as? String == "pair", value["nonce"] as? String == self.nonce,
                          let text = value["url"] as? String, let url = URL(string: text), let pairing = ReceiverPairing(url: url),
                          pairing.remoteAvailable, pairing.room == self.receiver.room {
                    self.receiver.key = pairing.key
                    self.openRelay()
                    DispatchQueue.main.async { self.paired?(url) }
                }
            }
            if error == nil { self.receiveDirect(connection) }
            else { connection.cancel(); self.udp = nil }
        }
    }

    private func openRelay() {
        guard active, receiver.remoteAvailable, websocket == nil, ProcessInfo.processInfo.systemUptime >= reconnectAt else { return }
        reconnectAt = ProcessInfo.processInfo.systemUptime + 3
        relayReady = false
        sending = false
        let socket = URLSession.shared.webSocketTask(with: relayURL)
        websocket = socket
        socket.resume()
        let hello = ["role": "phone", "room": receiver.room, "key": receiver.key]
        if let data = try? JSONSerialization.data(withJSONObject: hello), let text = String(data: data, encoding: .utf8) {
            socket.send(.string(text)) { [weak self, weak socket] error in
                if error != nil { self?.queue.async { if self?.websocket === socket { self?.resetRelay() } } }
            }
        }
        receiveRelay(socket)
    }

    private func receiveRelay(_ socket: URLSessionWebSocketTask) {
        socket.receive { [weak self, weak socket] result in
            guard let self, let socket else { return }
            self.queue.async {
                guard self.active, self.websocket === socket else { return }
                switch result {
                case .success(let message):
                    let data: Data
                    switch message {
                    case .data(let value): data = value
                    case .string(let value): data = Data(value.utf8)
                    @unknown default: return
                    }
                    if let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                        if value["type"] as? String == "ready" { self.relayReady = true }
                        if value["type"] as? String == "ack", value["session"] as? String == self.session { self.remoteAck = ProcessInfo.processInfo.systemUptime }
                    }
                    self.receiveRelay(socket)
                case .failure:
                    self.resetRelay()
                }
            }
        }
    }

    private func resetRelay() {
        relayReady = false
        websocket?.cancel(with: .goingAway, reason: nil)
        websocket = nil
    }

    private func tick() {
        guard active else { return }
        if udp == nil { openDirect() }
        openRelay()
        let ping: [String: Any] = ["type": "ping", "code": receiver.code, "session": session]
        if let data = try? JSONSerialization.data(withJSONObject: ping) { udp?.send(content: data, completion: .contentProcessed { _ in }) }
        if !receiver.remoteAvailable {
            let pair = ["type": "pair", "code": receiver.code, "nonce": nonce]
            if let data = try? JSONSerialization.data(withJSONObject: pair) { udp?.send(content: data, completion: .contentProcessed { _ in }) }
        }
        let now = ProcessInfo.processInfo.systemUptime
        let local = now - localAck < 1.5
        let remote = now - remoteAck < 2
        let text = local ? "같은 Wi-Fi로 연결됐어요." : remote ? "서울 중계소로 연결됐어요." : "PC에서 아이폰 자동 연결을 켜 주세요. 연결을 다시 시도하고 있어요."
        DispatchQueue.main.async { self.status?(local || remote, text) }
    }

    func send(_ packet: FacePacket) {
        guard active, let data = try? JSONEncoder().encode(packet) else { return }
        sequence = packet.sequence
        if ProcessInfo.processInfo.systemUptime - localAck < 1.5 {
            udp?.send(content: data, completion: .contentProcessed { _ in })
        } else if relayReady, !sending, let socket = websocket, let text = String(data: data, encoding: .utf8) {
            sending = true
            socket.send(.string(text)) { [weak self, weak socket] error in
                guard let self else { return }
                self.queue.async { if self.websocket === socket { self.sending = false; if error != nil { self.resetRelay() } } }
            }
        } else {
            udp?.send(content: data, completion: .contentProcessed { _ in })
        }
    }

    func stop() {
        queue.async {
            if self.active {
                var packet = FacePacket(code: self.receiver.code, session: self.session, sequence: self.sequence + 1, tracked: false, rotation: .init(yaw: 0, pitch: 0, roll: 0), blendShapes: [:])
                packet.type = "stop"
                self.send(packet)
            }
            self.active = false
            self.timer?.cancel()
            self.timer = nil
            self.udp?.cancel()
            self.udp = nil
            self.resetRelay()
        }
    }
}
