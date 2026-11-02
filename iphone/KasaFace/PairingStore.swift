import Foundation
import Security

struct ReceiverPairing {
    var host = ""
    var port: UInt16 = 4782
    var code = ""
    var room = ""
    var key = ""
    var name = "등록한 PC"
    var remoteAvailable: Bool { room.count == 32 && key.count == 64 }
    static let relay = URL(string: "wss://face.debimarlene.com/socket")!
    init() {}
    init?(url: URL) {
        guard let components = URLComponents(url: url, resolvingAgainstBaseURL: false), components.scheme == "kasaface", components.host == "connect" else { return nil }
        let items = components.queryItems ?? []
        func value(_ name: String) -> String { items.first(where: { $0.name == name })?.value ?? "" }
        host = value("host")
        port = UInt16(value("port")) ?? 4782
        code = value("code")
        room = value("room")
        key = value("key")
        name = value("name").isEmpty ? "등록한 PC" : value("name")
        guard port >= 1024, code.count == 8, code.allSatisfy({ "0123456789".contains($0) }),
              !host.contains("://"), !host.contains("/"),
              (room.isEmpty || (room.count == 32 && room.allSatisfy({ "0123456789abcdef".contains($0) }))),
              (key.isEmpty || (key.count == 64 && key.allSatisfy({ "0123456789abcdef".contains($0) }))) else { return nil }
    }
}

enum PairingStore {
    private static let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                             kSecAttrService as String: "com.kasa.studio.face.pairing",
                                             kSecAttrAccount as String: "receiver"]
    static func read() -> URL? {
        var request = query
        request[kSecReturnData as String] = true
        var result: CFTypeRef?
        guard SecItemCopyMatching(request as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data, let value = String(data: data, encoding: .utf8) else { return nil }
        return URL(string: value)
    }
    static func save(_ url: URL) {
        let data = Data(url.absoluteString.utf8)
        if SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary) == errSecItemNotFound {
            var item = query
            item[kSecValueData as String] = data
            item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            SecItemAdd(item as CFDictionary, nil)
        }
    }
}
