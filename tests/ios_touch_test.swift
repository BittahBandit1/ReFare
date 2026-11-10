import Foundation
import CoreGraphics
@main struct TouchTests {
    static func main() {
        for size in [CGSize(width: 780,height: 360), CGSize(width: 1280,height: 834),
                     CGSize(width: 375,height: 812), CGSize(width: 568,height: 320)] {
            let bounds = CGRect(origin: .zero,size: size)
            let safe = bounds.insetBy(dx: 16,dy: 24)
            let layout = MobileControlLayout(bounds: bounds,safe: safe)
            for region in layout.regions {
                precondition(safe.contains(region.frame), "Control extends beyond safe area")
                precondition(layout.control(at: CGPoint(x: region.frame.midX,y: region.frame.midY)) == region.control)
            }
            for a in layout.regions { for b in layout.regions where a.control != b.control {
                precondition(!a.frame.intersects(b.frame), "Overlapping controls: \(a.control) and \(b.control), \(size)")
            } }
        }
        let steering = MobileControlLayout.gameInput(held: [.left,.drive])
        precondition(steering[14] == -1 && steering[0] == 1 && steering[13] == 1)
        let gas = MobileControlLayout.gameInput(held: [.drive])
        precondition(gas[14] == 0 && gas[13] == 1, "Releasing steering must retain the other finger's accelerator")
        let reverse = MobileControlLayout.gameInput(held: [.reverse,.right])
        precondition(reverse[1] == 1 && reverse[0] == nil && reverse[13] == 1 && reverse[14] == 1)
        let brakes = MobileControlLayout.gameInput(held: [.brake])
        precondition(brakes[12] == 1 && brakes[13] == nil)
        let released = MobileControlLayout.gameInput(held: [])
        precondition(released[0] == nil && released[1] == nil && released[13] == nil)
        print("PASS: safe layouts in portrait and landscape, distinct hit targets, simultaneous steering/drive, reverse, brake, and release")
    }
}
