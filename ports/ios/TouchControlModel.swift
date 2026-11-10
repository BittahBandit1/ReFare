import Foundation
import CoreGraphics

enum MobileControl: String, CaseIterable {
    case left = "◀", right = "▶", up = "▲", down = "▼"
    case drive = "D", reverse = "R", brake = "BRAKE", start = "START"
}
struct MobileControlRegion {
    let control: MobileControl
    let frame: CGRect
}
struct MobileControlLayout {
    let regions: [MobileControlRegion]
    init(bounds: CGRect, safe: CGRect) {
        let unit = min(safe.width / 380, min(1.3, max(0.8, min(bounds.width, bounds.height) / 390)))
        let edge = 16 * unit
        let size = 76 * unit
        let gap = 12 * unit
        let left = safe.minX + edge
        let bottom = safe.maxY - edge
        let mini = max(44, 38 * unit)
        let miniWidth = max(44, 48 * unit)
        let y = bottom - size - mini - 4 * unit
        let right = safe.maxX - edge - size
        regions = [
            .init(control: .left, frame: CGRect(x: left, y: y, width: size, height: size)),
            .init(control: .right, frame: CGRect(x: left + size + gap, y: y, width: size, height: size)),
            .init(control: .up, frame: CGRect(x: left + size + gap/2 - miniWidth/2, y: y - mini - 4*unit, width: miniWidth, height: mini)),
            .init(control: .down, frame: CGRect(x: left + size + gap/2 - miniWidth/2, y: bottom - mini, width: miniWidth, height: mini)),
            .init(control: .drive, frame: CGRect(x: right, y: bottom - size*2 - gap, width: size, height: size)),
            .init(control: .reverse, frame: CGRect(x: right, y: bottom - size, width: size, height: size)),
            .init(control: .brake, frame: CGRect(x: right - size - gap, y: bottom - 46*unit, width: size, height: 46*unit)),
            .init(control: .start, frame: CGRect(x: safe.midX - 44*unit, y: safe.minY + edge, width: 88*unit, height: 36*unit))]
    }
    func control(at point: CGPoint) -> MobileControl? {
        regions.first { $0.frame.contains(point) }?.control
    }
    static func gameInput(held: Set<MobileControl>) -> [Int: Double] {
        var input: [Int: Double] = [:]
        input[14] = (held.contains(.right) ? 1 : 0) - (held.contains(.left) ? 1 : 0)
        input[15] = (held.contains(.up) ? 1 : 0) - (held.contains(.down) ? 1 : 0)
        // GameCube defaults: A = Drive/confirm, B = Reverse/back, R = accelerator.
        if held.contains(.reverse) { input[1] = 1; input[11] = 1; input[13] = 1 }
        else if held.contains(.drive) { input[0] = 1; input[11] = 1; input[13] = 1 }
        if held.contains(.brake) { input[10] = 1; input[12] = 1 }
        if held.contains(.start) { input[5] = 1 }
        return input
    }
}
