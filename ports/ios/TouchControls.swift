import SwiftUI
import UIKit

final class MobileControlCanvas: UIView {
    var changed: ([Int: Double]) -> Void = { _ in }
    var activity: () -> Void = {}
    var showControls = true { didSet {
        if oldValue != showControls { clearTouches(); setNeedsDisplay() }
    } }
    var menuOpen = false { didSet {
        if oldValue != menuOpen { clearTouches(); setNeedsDisplay() }
    } }
    private var fingers: [UITouch: CGPoint] = [:]
    private var held = Set<MobileControl>()
    private var menuFinger: UITouch?
    private var menuStart = CGPoint.zero
    private var previousMenuPoint = CGPoint.zero
    private var scrolling = false
    private var layout: MobileControlLayout {
        MobileControlLayout(bounds: bounds, safe: bounds.inset(by: safeAreaInsets))
    }
    override var editingInteractionConfiguration: UIEditingInteractionConfiguration { .none }
    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        isOpaque = false
        backgroundColor = .clear
        let three = UITapGestureRecognizer(target: self, action: #selector(threeFingerTap))
        three.numberOfTouchesRequired = 3
        three.cancelsTouchesInView = true
        addGestureRecognizer(three)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override func layoutSubviews() { super.layoutSubviews(); clearTouches(); setNeedsDisplay() }
    @objc private func threeFingerTap() { activity(); clearTouches(); ReFareToggleMenu() }
    func clearTouches() {
        fingers.removeAll(); held.removeAll(); changed([:])
        if menuFinger != nil { pointer(previousMenuPoint, down: false) }
        menuFinger = nil; scrolling = false; setNeedsDisplay()
    }
    private func pointer(_ point: CGPoint, down: Bool, scroll: Double = 0) {
        ReFareMenuPointer(Double(point.x / max(1,bounds.width)), Double(point.y / max(1,bounds.height)), down ? 1 : 0, scroll)
    }
    private func updateHeld() {
        held = Set(fingers.values.compactMap { layout.control(at: $0) })
        changed(MobileControlLayout.gameInput(held: held))
        setNeedsDisplay()
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        activity()
        if menuOpen {
            if menuFinger == nil, let finger = touches.first {
                menuFinger = finger; menuStart = finger.location(in: self)
                previousMenuPoint = menuStart; scrolling = false; pointer(menuStart, down: true)
            }
        } else if showControls {
            for finger in touches { fingers[finger] = finger.location(in: self) }
            if fingers.count >= 3 { held.removeAll(); changed([:]); setNeedsDisplay() }
            else { updateHeld() }
        }
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        activity()
        if menuOpen, let finger = menuFinger, touches.contains(finger) {
            let point = finger.location(in: self)
            if !scrolling && hypot(point.x-menuStart.x, point.y-menuStart.y) > 9 {
                scrolling = true; pointer(menuStart, down: false)
            }
            pointer(point, down: !scrolling, scroll: scrolling ? Double((point.y-previousMenuPoint.y)/40) : 0)
            previousMenuPoint = point
        } else if !menuOpen && showControls {
            for finger in touches { fingers[finger] = finger.location(in: self) }
            if fingers.count < 3 { updateHeld() }
        }
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        activity()
        if let finger = menuFinger, touches.contains(finger) { pointer(finger.location(in: self), down: false); menuFinger = nil }
        for finger in touches { fingers.removeValue(forKey: finger) }
        if !menuOpen && showControls { updateHeld() }
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { clearTouches() }
    override func draw(_ rect: CGRect) {
        guard !menuOpen && showControls else { return }
        for region in layout.regions {
            let selected = held.contains(region.control)
            let color: UIColor
            switch region.control {
            case .drive: color = UIColor(red: 0.26, green: 0.85, blue: 0.28, alpha: 1)
            case .reverse: color = UIColor(red: 1, green: 0.35, blue: 0.23, alpha: 1)
            default: color = UIColor(red: 1, green: 0.81, blue: 0.15, alpha: 1)
            }
            UIColor.black.withAlphaComponent(selected ? 0.72 : 0.4).setFill()
            let path = UIBezierPath(roundedRect: region.frame, cornerRadius: region.control == .start ? 18 : 12)
            path.fill(); color.withAlphaComponent(selected ? 1 : 0.72).setStroke()
            path.lineWidth = selected ? 3 : 1.5; path.stroke()
            let small = region.control == .start || region.control == .brake
            let font = UIFont.systemFont(ofSize: small ? min(18,region.frame.height*0.35) : region.frame.height*0.5, weight: .heavy)
            let text = region.control.rawValue as NSString
            let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color]
            let size = text.size(withAttributes: attributes)
            text.draw(at: CGPoint(x: region.frame.midX-size.width/2, y: region.frame.midY-size.height/2), withAttributes: attributes)
        }
    }
}
struct MobileTouchControls: UIViewRepresentable {
    var menuOpen: Bool
    var showControls: Bool
    var enabled: Bool
    var changed: ([Int: Double]) -> Void
    var activity: () -> Void
    func makeUIView(context: Context) -> MobileControlCanvas { MobileControlCanvas() }
    func updateUIView(_ view: MobileControlCanvas, context: Context) {
        view.changed = changed; view.activity = activity; view.menuOpen = menuOpen; view.showControls = showControls
        view.isUserInteractionEnabled = enabled
        view.isHidden = !enabled
        if !enabled { view.clearTouches() }
    }
}
