import AppKit
import Foundation

let root = URL(fileURLWithPath: CommandLine.arguments[1])
let application = NSApplication.shared
application.setActivationPolicy(.prohibited)
let source = root.appendingPathComponent("native/assets/StudioMark.svg")
guard let image = NSImage(contentsOf: source) else { fatalError("SVG 아이콘을 읽을 수 없어요.") }
let iconset = root.appendingPathComponent("artifacts/native/refinement/Studio.iconset")
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
func render(_ size: Int, opaque: Bool = false) -> Data {
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    NSGraphicsContext.current?.imageInterpolation = .high
    if opaque { NSColor(red: 251/255, green: 250/255, blue: 246/255, alpha: 1).setFill(); NSRect(x: 0, y: 0, width: size, height: size).fill() }
    image.draw(in: NSRect(x: 0, y: 0, width: size, height: size), from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()
    return bitmap.representation(using: .png, properties: [:])!
}
for size in [16, 32, 128, 256, 512] {
    try render(size).write(to: iconset.appendingPathComponent("icon_\(size)x\(size).png"))
    try render(size * 2).write(to: iconset.appendingPathComponent("icon_\(size)x\(size)@2x.png"))
}
try render(1024).write(to: root.appendingPathComponent("native/assets/StudioMark.png"))
let phone = root.appendingPathComponent("iphone/Assets.xcassets/AppIcon.appiconset")
try FileManager.default.createDirectory(at: phone, withIntermediateDirectories: true)
try render(1024, opaque: true).write(to: phone.appendingPathComponent("AppIcon.png"))
let manifest = """
{"images":[{"filename":"AppIcon.png","idiom":"universal","platform":"ios","size":"1024x1024"}],"info":{"author":"xcode","version":1}}
"""
try Data(manifest.utf8).write(to: phone.appendingPathComponent("Contents.json"))
print(iconset.path)
