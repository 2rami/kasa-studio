import Foundation
import simd

struct FacePacket: Encodable {
    struct Rotation: Encodable, Equatable {
        var yaw: Float
        var pitch: Float
        var roll: Float
    }
    var version = 1
    var code: String
    var session: String
    var sequence: UInt64
    var tracked: Bool
    var rotation: Rotation
    var blendShapes: [String: Float]
    var type = "frame"

    static func rotation(from matrix: simd_float4x4) -> Rotation {
        let scale: Float = 180 / .pi
        return Rotation(
            yaw: atan2(matrix.columns.2.x, matrix.columns.2.z) * scale,
            pitch: atan2(-matrix.columns.2.y, sqrt(matrix.columns.2.x * matrix.columns.2.x + matrix.columns.2.z * matrix.columns.2.z)) * scale,
            roll: atan2(matrix.columns.0.y, matrix.columns.1.y) * scale
        )
    }
}
