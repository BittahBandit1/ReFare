import SwiftUI
import UIKit

struct BuilderView: View {
    let gameAvailable: Bool
    let importing: Bool
    @Binding var building: Bool
    @Environment(\.scenePhase) private var scenePhase
    @State private var job: GameBuilder?
    @State private var status = "Build a separate personal gameplay IPA on this device. Sign and install it with your signing tool; gameplay needs no JIT."
    @State private var exported: URL?
    @State private var sharing = false
    @State private var showingLog = false
    @State private var log = ""

    var body: some View {
        VStack(spacing: 16) {
            Text("ReFare Builder").font(ReFareBranding.font(28))
            Text(status).multilineTextAlignment(.center)
            if building {
                ProgressView()
                Button("Cancel Build") { job?.cancel(); status = "Canceling after the current compiler step…" }
            }
            Button("Build Gameplay IPA") { start() }
                .buttonStyle(.borderedProminent).disabled(building || importing || !gameAvailable)
            if exported != nil {
                Button("Share Gameplay IPA…") { sharing = true }.disabled(building)
            }
            Button("View Build Log") {
                log = (try? String(contentsOf: GameBuilder.log, encoding: .utf8)) ?? "No build log yet."
                showingLog = true
            }.disabled(building)
            Text("The exported IPA contains your personal game code and assets. Keep it private. Signing and installation are separate steps.")
                .font(ReFareBranding.font(17)).foregroundStyle(ReFareBranding.ink)
        }
        .sheet(isPresented: $sharing) { if let url = exported { BuilderShare(items: [url]) } }
        .sheet(isPresented: $showingLog) {
            NavigationView { ScrollView { Text(log).font(.system(.caption, design: .monospaced)).textSelection(.enabled).padding() }
                .toolbar { Button("Close") { showingLog = false } } }
        }
        .onChange(of: scenePhase) { phase in
            if phase == .background && building {
                job?.cancel()
                status = "Build canceled because ReFare moved to the background. Keep the builder open while compiling."
            }
        }
    }

    private func start() {
        guard !building else { return }
        let builder = GameBuilder()
        job = builder; building = true; exported = nil
        UIApplication.shared.isIdleTimerDisabled = true
        status = "Preparing the build…"
        let worker = Thread {
            let result = Result { try builder.export { message in DispatchQueue.main.async { status = message } } }
            DispatchQueue.main.async {
                building = false; job = nil
                UIApplication.shared.isIdleTimerDisabled = false
                switch result {
                case .success(let url):
                    exported = url
                    status = "Your gameplay IPA is ready in the builder’s Documents folder in Files. Share it to your signing tool, then install the signed app."
                case .failure(let error):
                    status = error is CancellationError ? "Build canceled. Your imported disc is still available." : error.localizedDescription
                }
            }
        }
        // LLVM's frontend/code generator needs more stack than a GCD worker's
        // default. The standard clang driver also provisions a large stack.
        worker.stackSize = 16 * 1024 * 1024
        worker.qualityOfService = .userInitiated
        worker.start()
    }
}

private struct BuilderShare: UIViewControllerRepresentable {
    let items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
