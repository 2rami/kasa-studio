#if os(macOS)
import XCTest
import Network
@testable import KasaFaceProtocol

private final class UDPResponder {
    let listener: NWListener
    private let queue: DispatchQueue
    private var connections: [NWConnection] = []
    init(queue: DispatchQueue) throws { self.queue = queue; listener = try NWListener(using: .udp, on: .any) }
    func start(ready: XCTestExpectation) {
        listener.newConnectionHandler = { [weak self] connection in
            guard let self else { return }; self.connections.append(connection); connection.start(queue: self.queue); self.receive(connection)
        }
        listener.stateUpdateHandler = { state in if case .ready = state { ready.fulfill() } }
        listener.start(queue: queue)
    }
    private func receive(_ connection: NWConnection) {
        connection.receiveMessage { [weak self] data, _, _, error in
            if let data, let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any], let session = value["session"] as? String,
               let ack = try? JSONSerialization.data(withJSONObject: ["type": "ack", "session": session]) { connection.send(content: ack, completion: .contentProcessed { _ in }) }
            if error == nil { self?.receive(connection) }
        }
    }
    func stop() { queue.async { for connection in self.connections { connection.cancel() }; self.listener.cancel() } }
}

final class FaceTransportTests: XCTestCase {
    @MainActor
    func testDirectAndRelaySwitch() async throws {
        guard let value = ProcessInfo.processInfo.environment["KASA_FACE_TEST_RELAY_URL"], let relay = URL(string: value) else { throw XCTSkip("KASA_FACE_TEST_RELAY_URL is required") }
        let room = UUID().uuidString.replacingOccurrences(of: "-", with: "").lowercased()
        let key = room + room
        let host = URLSession.shared.webSocketTask(with: relay)
        host.resume()
        let hello = try JSONSerialization.data(withJSONObject: ["role": "host", "room": room, "key": key])
        try await host.send(.string(String(decoding: hello, as: UTF8.self)))
        _ = try await host.receive()
        let responder = Task {
            do {
                while !Task.isCancelled {
                    let incoming = try await host.receive()
                    guard case .string(let text) = incoming, let value = try JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any], value["type"] as? String == "frame" else { continue }
                    let data = try JSONSerialization.data(withJSONObject: ["type": "ack", "session": value["session"] as? String ?? "", "sequence": value["sequence"] as? Int ?? 0])
                    try await host.send(.string(String(decoding: data, as: UTF8.self)))
                }
            } catch {}
        }
        let queue = DispatchQueue(label: "face.transport.test")
        let transport = FaceTransport(queue: queue, relayURL: relay)
        let remoteFirst = expectation(description: "relay connected")
        let direct = expectation(description: "direct connected")
        let remoteAgain = expectation(description: "relay fallback")
        var phase = 0
        transport.status = { connected, text in
            guard connected else { return }
            if phase == 0 && text.contains("서울") { phase = 1; remoteFirst.fulfill() }
            else if phase == 1 && text.contains("Wi-Fi") { phase = 2; direct.fulfill() }
            else if phase == 3 && text.contains("서울") { phase = 4; remoteAgain.fulfill() }
        }
        var receiver = ReceiverPairing(); receiver.room = room; receiver.key = key; receiver.code = "12345678"
        transport.start(receiver: receiver, session: "swift-transport-test")
        let timer = DispatchSource.makeTimerSource(queue: queue)
        var sequence: UInt64 = 0
        timer.schedule(deadline: .now(), repeating: .milliseconds(34))
        timer.setEventHandler { sequence += 1; transport.send(FacePacket(code: "12345678", session: "swift-transport-test", sequence: sequence, tracked: true, rotation: .init(yaw: 10, pitch: 0, roll: 0), blendShapes: ["jawOpen": 0.5])) }
        timer.resume()
        await fulfillment(of: [remoteFirst], timeout: 10)
        let udp = try UDPResponder(queue: queue)
        let ready = expectation(description: "UDP ready")
        udp.start(ready: ready)
        await fulfillment(of: [ready], timeout: 5)
        let port = try XCTUnwrap(udp.listener.port?.rawValue)
        transport.addDirect(NearbyReceiver(id: room, name: "test", host: "127.0.0.1", port: port, code: "12345678"))
        await fulfillment(of: [direct], timeout: 5)
        phase = 3
        udp.stop()
        await fulfillment(of: [remoteAgain], timeout: 8)
        timer.cancel(); transport.stop()
        try await Task.sleep(nanoseconds: 100_000_000)
        host.cancel(with: .goingAway, reason: nil); responder.cancel()
        XCTAssertEqual(phase, 4)
    }
}
#endif
