import XCTest
import simd
import Darwin
#if canImport(KasaFaceProtocol)
@testable import KasaFaceProtocol
#else
@testable import KasaFace
#endif

final class FacePacketTests: XCTestCase {
    func testNeutralRotationAndProtocol() throws {
        XCTAssertEqual(FacePacket.rotation(from: matrix_identity_float4x4), .init(yaw: 0, pitch: 0, roll: 0))
        let packet = FacePacket(code: "12345678", session: "test", sequence: 1, tracked: true, rotation: .init(yaw: 10, pitch: 2, roll: -3), blendShapes: ["jawOpen": 0.5, "eyeBlinkLeft": 0.8])
        let data = try JSONEncoder().encode(packet)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["version"] as? Int, 1)
        XCTAssertEqual(json["sequence"] as? Int, 1)
        XCTAssertEqual(json["code"] as? String, "12345678")
    }
    func testRemotePairingRequiresFullKey() {
        let base = "kasaface://connect?port=4782&code=12345678&room=" + String(repeating: "a", count: 32) + "&key="
        XCTAssertNil(ReceiverPairing(url: URL(string: base + String(repeating: "b", count: 48))!))
        XCTAssertTrue(ReceiverPairing(url: URL(string: base + String(repeating: "b", count: 64))!)?.remoteAvailable == true)
    }
    func testBonjourIPv4Address() {
        XCTAssertNil(PeerDiscovery.ipv4Address(Data()))
        var address = sockaddr_in(); address.sin_family = sa_family_t(AF_INET)
        inet_pton(AF_INET, "192.168.12.34", &address.sin_addr)
        let data = withUnsafeBytes(of: &address) { Data($0) }
        XCTAssertEqual(PeerDiscovery.ipv4Address(data), "192.168.12.34")
    }
}
