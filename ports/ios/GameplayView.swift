import SwiftUI
import Combine
import UIKit
import Metal
import GameController
import AVFoundation

final class GameSurface: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
    var started = false
    override func layoutSubviews() {
        super.layoutSubviews()
        guard let metal = layer as? CAMetalLayer else { return }
        let scale = window?.screen.nativeScale ?? UIScreen.main.nativeScale
        contentScaleFactor = scale
        ReFareDisplayRate(Double(window?.screen.maximumFramesPerSecond ?? 60))
        metal.contentsScale = scale
        metal.drawableSize = CGSize(width: bounds.width * scale, height: bounds.height * scale)
        if started { ReFareResize() }
        guard !started, window != nil, bounds.width > bounds.height, bounds.height > 0 else { return }
        guard MTLCreateSystemDefaultDevice() != nil else { return }
        started = true
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let root = base.appendingPathComponent("ReFare/GCTE51").path
        let user = base.appendingPathComponent("ReFare/user").path
        ReFareStart(Unmanaged.passUnretained(self).toOpaque(),
                    Unmanaged.passUnretained(metal).toOpaque(), Float(scale), root, user)
    }
    override func didMoveToWindow() { super.didMoveToWindow(); setNeedsLayout() }
}
struct GameSurfaceView: UIViewRepresentable {
    func makeUIView(context: Context) -> GameSurface {
        let view = GameSurface(); view.backgroundColor = .black; return view
    }
    func updateUIView(_ view: GameSurface, context: Context) {}
}

private final class TouchInputStore {
    var values: [Int: Double] = [:]
    var lastActivity = ProcessInfo.processInfo.systemUptime
}

struct GameplayView: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase
    @State private var message = "Starting game…"
    @State private var finished = false
    @State private var quitting = false
    @State private var interrupted = false
    @State private var input = TouchInputStore()
    @State private var menuOpen = false
    @State private var controllerConnected = GCController.controllers().contains { $0.extendedGamepad != nil }
    @State private var exitVisible = true
    private let timer = Timer.publish(every: 1.0 / 60.0, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            GameSurfaceView().ignoresSafeArea()
            MobileTouchControls(menuOpen: menuOpen, showControls: !controllerConnected, enabled: !finished && !quitting && scenePhase == .active && !interrupted,
                                changed: { input.values = $0 },
                                activity: { input.lastActivity = ProcessInfo.processInfo.systemUptime }).ignoresSafeArea()
            if finished || quitting {
                VStack {
                    HStack {
                        Button(finished ? "Back" : "Stopping…") { if finished { dismiss() } }
                            .buttonStyle(.borderedProminent).disabled(!finished)
                        Spacer()
                        Text(message).foregroundColor(.white).padding().background(Color.black.opacity(0.7))
                    }
                    Spacer()
                }.padding()
            } else if exitVisible {
                VStack {
                    HStack {
                        Button("EXIT") {
                            quitting = true
                            ReFareStop()
                        }.buttonStyle(.borderedProminent)
                        Spacer()
                    }
                    Spacer()
                }.padding().transition(.opacity)
            }
        }
        .statusBar(hidden: true)
        .onReceive(timer) { _ in
            var buffer = [CChar](repeating: 0, count: 1024)
            let state = ReFareStatus(&buffer, Int32(buffer.count))
            let shouldShowExit = ProcessInfo.processInfo.systemUptime - input.lastActivity < 3
            if exitVisible != shouldShowExit {
                withAnimation(.easeInOut(duration: 0.15)) { exitVisible = shouldShowExit }
            }
            menuOpen = ReFareMenuOpen() != 0
            if state >= 2 {
                UIApplication.shared.isIdleTimerDisabled = false
                finished = true; message = String(cString: buffer)
                if quitting { dismiss() }
            }
            if scenePhase != .active || interrupted { ReFarePause(1) }
            updateInput()
        }
        .onReceive(NotificationCenter.default.publisher(for: AVAudioSession.interruptionNotification)) { notification in
            let raw = notification.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt
            interrupted = raw == AVAudioSession.InterruptionType.began.rawValue
            if !interrupted && scenePhase == .active { try? AVAudioSession.sharedInstance().setActive(true) }
            ReFarePause(interrupted || scenePhase != .active ? 1 : 0)
        }
        .onChange(of: scenePhase) { phase in
            if phase != .active { input.values = [:]; for id in 0...17 { ReFareInput(Int32(id), 0) } }
            if phase == .active {
                input.lastActivity = ProcessInfo.processInfo.systemUptime
                try? AVAudioSession.sharedInstance().setActive(true)
            }
            ReFarePause(phase == .active && !interrupted ? 0 : 1)
            if phase != .active { try? AVAudioSession.sharedInstance().setActive(false) }
            UIApplication.shared.isIdleTimerDisabled = phase == .active && !finished
        }
        .onAppear {
            input.lastActivity = ProcessInfo.processInfo.systemUptime
            exitVisible = true
            UIApplication.shared.isIdleTimerDisabled = true
        }
        .onDisappear {
            UIApplication.shared.isIdleTimerDisabled = false
            ReFareStop()
            for id in 0...17 { ReFareInput(Int32(id), 0) }
        }
    }

    private func updateInput() {
        let gamepad = GCController.controllers().compactMap { $0.extendedGamepad }.first
        let connected = gamepad != nil
        if connected != controllerConnected {
            input.values = [:]
            controllerConnected = connected
        }
        var values = input.values
        var menuMask: UInt32 = 0
        // Touch shoulder buttons include analog travel, as a real GC trigger does.
        values[12] = max(values[12] ?? 0, values[10] ?? 0)
        values[13] = max(values[13] ?? 0, values[11] ?? 0)
        if scenePhase == .active, let pad = gamepad {
            let nav: [(Int, Bool)] = [
                (0, pad.leftShoulder.isPressed),
                (1, pad.dpad.up.isPressed || pad.leftThumbstick.yAxis.value > 0.5),
                (2, pad.dpad.down.isPressed || pad.leftThumbstick.yAxis.value < -0.5),
                (3, pad.dpad.left.isPressed || pad.leftThumbstick.xAxis.value < -0.5),
                (4, pad.dpad.right.isPressed || pad.leftThumbstick.xAxis.value > 0.5),
                (5, pad.buttonB.isPressed), (6, pad.buttonA.isPressed)]
            for (id, pressed) in nav where pressed { menuMask |= 1 << id }
            let controls: [(Int, Double)] = [
                (0, Double(pad.buttonA.value)), (1, Double(pad.buttonB.value)),
                (2, Double(pad.buttonX.value)), (3, Double(pad.buttonY.value)),
                (4, Double(pad.rightShoulder.value)), (5, Double(pad.buttonMenu.value)),
                (6, Double(pad.dpad.up.value)), (7, Double(pad.dpad.down.value)),
                (8, Double(pad.dpad.left.value)), (9, Double(pad.dpad.right.value)),
                (10, Double(pad.leftShoulder.value)), (11, Double(pad.rightShoulder.value)),
                (12, Double(pad.leftTrigger.value)), (13, Double(pad.rightTrigger.value)),
                (14, Double(pad.leftThumbstick.xAxis.value)), (15, Double(pad.leftThumbstick.yAxis.value)),
                (16, Double(pad.rightThumbstick.xAxis.value)), (17, Double(pad.rightThumbstick.yAxis.value))]
            for (id, value) in controls where abs(value) > abs(values[id] ?? 0) { values[id] = value }
        }
        ReFareMenuButtons(scenePhase == .active && !interrupted ? menuMask : 0)
        for id in 0...17 { ReFareInput(Int32(id), scenePhase == .active && !interrupted && !menuOpen ? values[id] ?? 0 : 0) }
    }
}
