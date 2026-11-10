import SwiftUI
import UIKit

// This target validates the native import path. It is deliberately named
// ReFare Import until the game runtime can launch on iOS.
@main
struct ReFareImportApp: App {
    init() {
#if REFARE_GAMEPLAY
        // Initialize HUD support before the first Metal device is created.
        setenv("MTL_HUD_ENABLED", "1", 0)
        UserDefaults.standard.set(true, forKey: "MetalForceHudEnabled")
#endif
    }
    var body: some Scene { WindowGroup { ImportView() } }
}

struct ImportView: View {
    @State private var showingLog = false
    @State private var gameplayLog = ""
    @State private var playing = false
    @State private var choosing = false
    @State private var importing = false
    @State private var choosingFolder = false
    @State private var folder: URL?
    @State private var folderAccess = false
    @State private var discs: [URL] = []
    @State private var message = "Import your Crazy Taxi USA disc from Files. ISO, GCM and GameCube CISO are supported."

    var body: some View {
        NavigationView {
            ScrollView { VStack(spacing: 24) {
                Image(systemName: "externaldrive").font(.system(size: 56))
                Text("ReFare Import").font(.largeTitle.bold())
                Text(message).multilineTextAlignment(.center).accessibilityIdentifier("importStatus")
                if importing { ProgressView("Importing…") }
                Button("Choose Disc…") { choosing = true }
                    .buttonStyle(.borderedProminent).disabled(importing)
                Button("Choose Disc Folder…") { choosingFolder = true }
                    .disabled(importing)
                Text("If a disc cannot be selected in Files, choose its folder instead, then select the disc below.")
                    .font(.footnote).foregroundStyle(.secondary)
                ForEach(discs, id: \.self) { disc in
                    Button(disc.lastPathComponent) { startImport(disc) }.disabled(importing)
                }
#if REFARE_GAMEPLAY
                Button("Play Crazy Taxi") { playing = true }
                    .buttonStyle(.borderedProminent).disabled(importing || !hasGame)
                Button("View Gameplay Log") {
                    gameplayLog = readGameplayLog()
                    showingLog = true
                }
#else
                Text("Gameplay is not included in this import-only build.").font(.footnote)
#endif
                Text("Build 0.4.1 (12)").font(.caption).foregroundStyle(.secondary)
                Text("iOS development build. Import your own supported disc before playing.")
                    .font(.footnote).foregroundStyle(.secondary)
            }.padding(32) }
            .sheet(isPresented: $choosing) {
                DiscPicker { url in
                    choosing = false
                    startImport(url)
                } cancelled: {
                    choosing = false
                    message = "No disc selected. Choose an ISO, GCM or GameCube CISO file to import."
                }
            }
            .sheet(isPresented: $choosingFolder) {
                DiscPicker(selectFolder: true) { url in
                    choosingFolder = false
                    openFolder(url)
                } cancelled: { choosingFolder = false }
            }
#if REFARE_GAMEPLAY
            .sheet(isPresented: $showingLog) {
                VStack {
                    HStack {
                        Button("Close") { showingLog = false }
                        Spacer()
                        Button("Copy Log") { UIPasteboard.general.string = gameplayLog }
                    }.padding()
                    ScrollView { Text(gameplayLog).font(.system(.caption, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading).padding() }
                }
            }
            .fullScreenCover(isPresented: $playing) { GameplayView().interactiveDismissDisabled() }
#endif
            .onOpenURL { url in startImport(url) }
        }.navigationViewStyle(.stack)
    }

    private func readGameplayLog() -> String {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        do {
            let file = try FileHandle(forReadingFrom: base.appendingPathComponent("ReFare/user/gameplay.log"))
            defer { try? file.close() }
            let size = try file.seekToEnd()
            try file.seek(toOffset: size > 131072 ? size - 131072 : 0)
            let data = try file.readToEnd() ?? Data()
            return String(decoding: data, as: UTF8.self)
        } catch { return "No gameplay log is available yet. Try Play first." }
    }

    private var hasGame: Bool {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return FileManager.default.fileExists(atPath: base.appendingPathComponent("ReFare/GCTE51/sys/main.dol").path)
    }

    private func openFolder(_ url: URL) {
        if folderAccess, let old = folder { old.stopAccessingSecurityScopedResource() }
        discs = []
        folder = url
        folderAccess = url.startAccessingSecurityScopedResource()
        do {
            discs = try DiscImporter.discs(in: url)
            message = discs.isEmpty ? "No ISO, GCM or CISO files found in this folder." : "Select your disc below to import it."
        } catch { message = "Could not read the folder: \(error.localizedDescription)" }
    }

    private func startImport(_ url: URL) {
        guard !importing else { return }
        // Acquire access immediately while handling the picker/open callback,
        // then hold it until all coordinated reads finish in the worker.
        let accessed = url.startAccessingSecurityScopedResource()
        importing = true
        message = "Opening \(url.lastPathComponent)… Downloading if needed, then checking and importing the disc."
        Task {
            defer {
                if accessed { url.stopAccessingSecurityScopedResource() }
                importing = false
            }
            do {
                let destination = try FileManager.default.url(for: .applicationSupportDirectory,
                    in: .userDomainMask, appropriateFor: nil, create: true)
                    .appendingPathComponent("ReFare/GCTE51", isDirectory: true)
                let count = try await Task.detached(priority: .userInitiated) {
                    return try DiscImporter.extractFromFileProvider(url, to: destination)
                }.value
                message = "Imported and verified \(count) game files. Your disc is ready."
            } catch { message = error.localizedDescription }
        }
    }
}
