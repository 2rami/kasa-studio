// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "KasaFaceProtocol",
    platforms: [.macOS(.v13)],
    products: [.library(name: "KasaFaceProtocol", targets: ["KasaFaceProtocol"])],
    targets: [
        .target(name: "KasaFaceProtocol", path: "KasaFace", exclude: ["FaceSender.swift", "KasaFaceApp.swift"], sources: ["FacePacket.swift", "FaceTransport.swift", "PeerDiscovery.swift", "PairingStore.swift"]),
        .testTarget(name: "KasaFaceProtocolTests", dependencies: ["KasaFaceProtocol"], path: "Tests")
    ]
)
