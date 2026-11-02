import ARKit
import AVFoundation
import Foundation
import UIKit

final class FaceSender: NSObject, ObservableObject, ARSessionDelegate {
    @Published var host = ""
    @Published var port = "4782"
    @Published var code = ""
    @Published private(set) var nearby: [NearbyReceiver] = []
    @Published private(set) var receiverName = ""
    @Published private(set) var running = false
    @Published private(set) var preparing = false
    @Published private(set) var connected = false
    @Published private(set) var faceFound = false
    @Published private(set) var status = "같은 Wi-Fi의 PC를 자동으로 찾고 있어요."

    let session = ARSession()
    private let queue = DispatchQueue(label: "com.kasa.studio.face.sender")
    private lazy var transport = FaceTransport(queue: queue)
    private let discovery = PeerDiscovery()
    private var saved = ReceiverPairing()
    private var waitingForPC = false
    private var active = false
    private var epoch = 0
    private var sequence: UInt64 = 0
    private var sessionID = UUID().uuidString
    private var pairingCode = ""
    private var sentAt: TimeInterval = 0
    private var publishedAt: TimeInterval = 0
    var remoteAvailable: Bool { saved.remoteAvailable }

    override init() {
        super.init()
        session.delegate = self
        session.delegateQueue = queue
        if let url = PairingStore.read(), let pairing = ReceiverPairing(url: url) { saved = pairing; host = pairing.host; port = String(pairing.port); code = pairing.code; receiverName = pairing.name }
        discovery.changed = { [weak self] receivers in
            guard let self else { return }
            self.nearby = receivers
            if self.running, let direct = receivers.first(where: { $0.id == self.saved.room }) { self.transport.addDirect(direct) }
            if self.waitingForPC, let direct = receivers.first(where: { $0.id == self.saved.room }) ?? (self.saved.room.isEmpty && receivers.count == 1 ? receivers.first : nil) {
                self.waitingForPC = false; self.preparing = false; self.connect(direct)
            }
        }
        discovery.start()
    }
    func attachPreview(_ view: ARSCNView) {
        view.session = session
        session.delegate = self
        session.delegateQueue = queue
    }

    func configure(from url: URL) {
        guard !running, let pairing = ReceiverPairing(url: url) else { return }
        saved = pairing; host = pairing.host; port = String(pairing.port); code = pairing.code; receiverName = pairing.name
        if pairing.remoteAvailable { PairingStore.save(url) }
        status = "PC를 등록했어요. 연결을 누르면 같은 Wi-Fi 또는 서울 경유로 연결돼요."
    }

    func connect(_ receiver: NearbyReceiver) {
        guard !running, !preparing else { return }
        if saved.room != receiver.id { saved = ReceiverPairing() }
        saved.room = receiver.id; saved.name = receiver.name
        host = receiver.host; port = String(receiver.port); code = receiver.code; receiverName = receiver.name
        start()
    }

    func start() {
        guard !running, !preparing else { return }
        guard ARFaceTrackingConfiguration.isSupported else { status = "이 기기는 ARKit 얼굴 추적을 지원하지 않아요."; return }
        if let direct = nearby.first(where: { $0.id == saved.room }) ?? (saved.room.isEmpty && nearby.count == 1 ? nearby.first : nil) {
            if saved.room != direct.id { saved = ReceiverPairing() }
            saved.room = direct.id; saved.name = direct.name
            host = direct.host; port = String(direct.port); code = direct.code; receiverName = direct.name
        }
        if host.isEmpty && !saved.remoteAvailable {
            waitingForPC = true; preparing = true; status = "PC에서 아이폰 자동 연결을 누르면 연결돼요."
            return
        }
        let address = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !address.contains("://"), !address.contains("/"), let number = UInt16(port), number >= 1024, code.count == 8, code.allSatisfy({ "0123456789".contains($0) }) else {
            status = "주변 PC를 선택하거나 PC의 QR을 처음 한 번 등록해 주세요."
            return
        }
        epoch += 1
        let current = epoch
        var pairing = saved; pairing.host = address; pairing.port = number; pairing.code = code
        preparing = true
        AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
            DispatchQueue.main.async {
                guard let self, self.epoch == current else { return }
                guard granted else { self.preparing = false; self.status = "설정에서 카메라 접근을 허용해 주세요."; return }
                self.begin(pairing)
            }
        }
    }

    private func begin(_ receiver: ReceiverPairing) {
        preparing = false
        running = true
        connected = false
        status = "맥 연결을 확인하고 있어요."
        UIApplication.shared.isIdleTimerDisabled = true
        let current = epoch
        let id = UUID().uuidString
        transport.status = { [weak self] connected, text in
            guard let self, self.running, self.epoch == current else { return }
            self.connected = connected; self.status = text
        }
        transport.paired = { [weak self] url in
            guard let self, self.running, self.epoch == current, let pairing = ReceiverPairing(url: url) else { return }
            self.saved = pairing; self.receiverName = pairing.name; PairingStore.save(url)
        }
        queue.async {
            self.active = true
            self.pairingCode = receiver.code
            self.sequence = 0
            self.sessionID = id
            self.sentAt = 0
        }
        transport.start(receiver: receiver, session: id)
        let configuration = ARFaceTrackingConfiguration()
        configuration.maximumNumberOfTrackedFaces = 1
        configuration.isLightEstimationEnabled = false
        session.run(configuration, options: [.resetTracking, .removeExistingAnchors])
    }

    func stop() {
        epoch += 1
        session.pause()
        running = false
        preparing = false
        faceFound = false
        connected = false
        waitingForPC = false
        UIApplication.shared.isIdleTimerDisabled = false
        transport.stop()
        queue.async { self.active = false }
    }

    func session(_ session: ARSession, didUpdate frame: ARFrame) {
        let time = ProcessInfo.processInfo.systemUptime
        guard active, time - sentAt >= 1 / 30 else { return }
        sentAt = time
        sequence += 1
        let face = frame.anchors.compactMap { $0 as? ARFaceAnchor }.first
        let tracked = face?.isTracked == true
        var rotation = FacePacket.Rotation(yaw: 0, pitch: 0, roll: 0)
        var shapes: [String: Float] = [:]
        if let face, tracked {
            rotation = FacePacket.rotation(from: frame.camera.viewMatrix(for: .portrait) * face.transform)
            for (key, value) in face.blendShapes { shapes[key.rawValue] = value.floatValue }
        }
        let packet = FacePacket(code: pairingCode, session: sessionID, sequence: sequence, tracked: tracked, rotation: rotation, blendShapes: shapes)
        transport.send(packet)
        if time - publishedAt > 0.2 { publishedAt = time; DispatchQueue.main.async { if self.running { self.faceFound = tracked } } }
    }

    func session(_ session: ARSession, didFailWithError error: Error) {
        DispatchQueue.main.async { self.stop(); self.status = "얼굴 추적 실패: \(error.localizedDescription)" }
    }

    func sessionWasInterrupted(_ session: ARSession) {
        DispatchQueue.main.async { self.stop(); self.status = "추적이 중단됐어요. 추적 시작을 다시 눌러 주세요." }
    }
}
