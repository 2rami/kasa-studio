import ARKit
import SwiftUI

@main
struct KasaFaceApp: App {
    @StateObject private var sender = FaceSender()
    @Environment(\.scenePhase) private var phase
    var body: some Scene {
        WindowGroup {
            FaceView(sender: sender)
                .onOpenURL { sender.configure(from: $0) }
                .onChange(of: phase) { _, state in if state == .background { sender.stop() } }
        }
    }
}

struct CameraView: UIViewRepresentable {
    let sender: FaceSender
    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeUIView(context: Context) -> ARSCNView {
        let view = ARSCNView(frame: .zero)
        sender.attachPreview(view)
        view.delegate = context.coordinator
        view.automaticallyUpdatesLighting = false
        view.preferredFramesPerSecond = 30
        return view
    }
    func updateUIView(_ uiView: ARSCNView, context: Context) {}
    final class Coordinator: NSObject, ARSCNViewDelegate {
        func renderer(_ renderer: SCNSceneRenderer, nodeFor anchor: ARAnchor) -> SCNNode? {
            guard anchor is ARFaceAnchor, let device = renderer.device, let geometry = ARSCNFaceGeometry(device: device) else { return nil }
            geometry.firstMaterial?.fillMode = .lines
            geometry.firstMaterial?.lightingModel = .constant
            geometry.firstMaterial?.diffuse.contents = UIColor(red: 0.86, green: 0.95, blue: 0.42, alpha: 0.7)
            return SCNNode(geometry: geometry)
        }
        func renderer(_ renderer: SCNSceneRenderer, didUpdate node: SCNNode, for anchor: ARAnchor) {
            if let face = anchor as? ARFaceAnchor, let geometry = node.geometry as? ARSCNFaceGeometry { geometry.update(from: face.geometry) }
        }
    }
}

struct FaceView: View {
    @ObservedObject var sender: FaceSender
    private let ink = Color(red: 0.06, green: 0.10, blue: 0.14)
    private let sky = Color(red: 0.10, green: 0.72, blue: 0.95)
    private let lime = Color(red: 0.86, green: 0.95, blue: 0.42)
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                HStack {
                    Text("Kasa Face").font(.system(size: 30, weight: .bold))
                    Spacer()
                    Circle().fill(sender.connected ? .green : .gray).frame(width: 8, height: 8)
                    Text(sender.connected ? "맥 연결됨" : "연결 전").font(.caption)
                }
                ZStack {
                    RoundedRectangle(cornerRadius: 18).fill(sky.opacity(0.15))
                    if sender.running {
                        CameraView(sender: sender).clipShape(RoundedRectangle(cornerRadius: 18))
                        VStack { Spacer(); Text(sender.faceFound ? "얼굴 추적 중" : "얼굴을 찾고 있어요").font(.caption).padding(9).background(.regularMaterial, in: Capsule()).padding(12) }
                    } else {
                        VStack(spacing: 12) { Image(systemName: "faceid").font(.system(size: 58, weight: .light)); Text("추적 시작을 누르면 카메라가 켜져요.").font(.callout) }
                    }
                }.frame(height: 290)
                if !sender.running {
                    VStack(alignment: .leading, spacing: 12) {
                        Text("주변 PC").font(.headline)
                        if sender.nearby.isEmpty { Text("PC에서 아이폰 자동 연결을 누르면 여기에 나타나요.").font(.callout).foregroundStyle(.secondary) }
                        ForEach(sender.nearby) { receiver in
                            Button { sender.connect(receiver) } label: {
                                HStack { Image(systemName: "desktopcomputer"); Text(receiver.name); Spacer(); Text("연결").fontWeight(.semibold) }
                                    .padding(16).background(sky.opacity(0.12), in: RoundedRectangle(cornerRadius: 12))
                            }.buttonStyle(.plain).disabled(sender.preparing)
                        }
                        if sender.remoteAvailable { Label(sender.receiverName + " · 외부망에서도 연결", systemImage: "checkmark.circle").font(.callout) }
                    }
                }
                Text(sender.status).font(.callout).foregroundStyle(.secondary).frame(maxWidth: .infinity, alignment: .leading)
                Button { if sender.running || sender.preparing { sender.stop() } else { sender.start() } } label: {
                    Text(sender.running || sender.preparing ? "추적 중지" : "연결하고 추적 시작").fontWeight(.semibold).frame(maxWidth: .infinity).padding(.vertical, 16).background(sender.running ? lime : sky, in: RoundedRectangle(cornerRadius: 12))
                }.buttonStyle(.plain)
                Text("같은 Wi-Fi에서는 바로 연결돼요. 셀룰러·다른 Wi-Fi에서는 처음 한 번 PC의 QR을 등록하면 서울 중계소로 자동 연결돼요. 얼굴 영상은 전송하지 않아요.")
                    .font(.footnote).foregroundStyle(.secondary)
                if !sender.running && !sender.preparing {
                    Button("연결 링크 붙여넣기") { if let text = UIPasteboard.general.string, let url = URL(string: text) { sender.configure(from: url) } }.font(.callout)
                    DisclosureGroup("직접 입력") {
                        VStack(spacing: 14) {
                            entry("맥 주소", text: $sender.host, placeholder: "192.168.0.10", keyboard: .numbersAndPunctuation)
                            HStack(alignment: .top, spacing: 16) {
                                entry("포트", text: $sender.port, placeholder: "4782", keyboard: .numberPad)
                                entry("연결 코드", text: $sender.code, placeholder: "8자리 숫자", keyboard: .numberPad)
                            }
                        }.padding(.top, 12)
                    }.font(.callout)
                }
            }.padding(24).foregroundStyle(ink)
        }.background(Color(red: 0.984, green: 0.980, blue: 0.965)).preferredColorScheme(.light)
    }
    private func entry(_ label: String, text: Binding<String>, placeholder: String, keyboard: UIKeyboardType) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            Text(label).font(.caption).foregroundStyle(.secondary)
            TextField(placeholder, text: text).keyboardType(keyboard).textInputAutocapitalization(.never).autocorrectionDisabled().padding(12).background(.white, in: RoundedRectangle(cornerRadius: 9))
        }
    }
}
