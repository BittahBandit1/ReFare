// ReFare launcher: a small SwiftUI window that turns the player's own Crazy
// Taxi (USA) disc image into a native build on their Mac, then launches it.
//
// The app bundle contains no game code or data. On first use the player picks
// (or drops) a .iso or .ciso; the launcher checks it is GCTE51, converts a
// CISO if needed, extracts it, and recompiles the game's code locally into
// ~/Library/Application Support/ReFare. Later launches go straight to Play.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

// MARK: - Locations

struct Paths {
  let bundleTools: URL     // Contents/MacOS: launcher, moderngekko-run/-port, dolrecomp
  let toolchain: URL       // Contents/Resources/toolchain: module sources the recompile needs
  let refare: URL          // Contents/Resources/refare: prepare script + config
  let support: URL         // ~/Library/Application Support/ReFare
  var extracted: URL { support.appendingPathComponent("game/extracted") }
  var prepared: URL { support.appendingPathComponent("game/prepared") }
  var modules: URL { support.appendingPathComponent("modules") }
  var user: URL { support.appendingPathComponent("user") }
  var logFile: URL { support.appendingPathComponent("launcher.log") }

  init() {
    let bundle = Bundle.main
    bundleTools = bundle.executableURL!.deletingLastPathComponent()
    let resources = bundle.resourceURL!
    toolchain = resources.appendingPathComponent("toolchain")
    refare = resources.appendingPathComponent("refare")
    support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("ReFare")
  }

  func tool(_ name: String) -> URL { bundleTools.appendingPathComponent(name) }

  /// Installed = the game was prepared and a native module was built for it.
  var isInstalled: Bool {
    let fm = FileManager.default
    guard fm.fileExists(atPath: prepared.appendingPathComponent("sys/main.dol").path),
          let walker = fm.enumerator(at: modules, includingPropertiesForKeys: nil)
    else { return false }
    for case let url as URL in walker where url.pathExtension == "dylib" { return true }
    return false
  }
}

// MARK: - Disc checks

enum DiscError: LocalizedError {
  case unreadable, notGameCube, wrongGame(String), badCISO(String)
  var errorDescription: String? {
    switch self {
    case .unreadable: return "The file couldn't be read."
    case .notGameCube: return "This isn't a GameCube disc image."
    case .wrongGame(let id):
      return "This disc is \(id), not Crazy Taxi (USA). ReFare needs the GCTE51 disc."
    case .badCISO(let why): return "This CISO file can't be used: \(why)."
    }
  }
}

struct DiscImage {
  let url: URL
  let isCISO: Bool

  static let gameID = "GCTE51"
  static let gameCubeMagic: [UInt8] = [0xC2, 0x33, 0x9F, 0x3D]
  static let discSize: UInt64 = 1_459_978_240

  /// Reads the disc header (from block 0 for a CISO) and checks it is GCTE51.
  static func inspect(_ url: URL) throws -> DiscImage {
    guard let handle = try? FileHandle(forReadingFrom: url) else { throw DiscError.unreadable }
    defer { try? handle.close() }
    guard let start = try handle.read(upToCount: 0x8000), start.count >= 0x20 else {
      throw DiscError.unreadable
    }
    var header = start
    var isCISO = false
    if start.starts(with: Array("CISO".utf8)) {
      isCISO = true
      guard start.count == 0x8000, start[8] == 1 else { throw DiscError.badCISO("no disc header") }
      guard let first = try handle.read(upToCount: 0x20), first.count == 0x20 else {
        throw DiscError.badCISO("truncated")
      }
      header = first
    }
    guard Array(header[0x1C..<0x20]) == gameCubeMagic else { throw DiscError.notGameCube }
    let id = String(decoding: header[0..<6], as: UTF8.self)
    guard id == gameID else { throw DiscError.wrongGame(id) }
    return DiscImage(url: url, isCISO: isCISO)
  }

  /// Expands a GameCube block-map CISO to a sparse ISO (as scripts/ciso-to-iso.py).
  func expandCISO(to destination: URL) throws {
    let src = try FileHandle(forReadingFrom: url)
    defer { try? src.close() }
    guard let header = try src.read(upToCount: 0x8000), header.count == 0x8000 else {
      throw DiscError.badCISO("truncated header")
    }
    let blockSize = header.withUnsafeBytes { UInt64($0.loadUnaligned(fromByteOffset: 4, as: UInt32.self).littleEndian) }
    guard blockSize > 0, blockSize <= 32 << 20 else { throw DiscError.badCISO("bad block size") }
    let map = header[8...]
    guard map.allSatisfy({ $0 <= 1 }) else { throw DiscError.badCISO("bad block map") }

    FileManager.default.createFile(atPath: destination.path, contents: nil)
    let out = try FileHandle(forWritingTo: destination)
    defer { try? out.close() }
    for (index, present) in map.enumerated() where present == 1 {
      let offset = UInt64(index) * blockSize
      guard offset < Self.discSize else { throw DiscError.badCISO("data beyond the disc") }
      guard let block = try src.read(upToCount: Int(blockSize)), UInt64(block.count) == blockSize else {
        throw DiscError.badCISO("truncated block")
      }
      try out.seek(toOffset: offset)
      try out.write(contentsOf: block.prefix(Int(min(blockSize, Self.discSize - offset))))
    }
    try out.truncate(atOffset: Self.discSize)
  }
}

// MARK: - Model

@MainActor
final class LauncherModel: ObservableObject {
  enum Phase: Equatable {
    case needsDisc
    case working(String)
    case ready
    case playing
    case failed(String)
    case missingTools(clt: Bool, cmake: Bool, ninja: Bool)
  }

  @Published var phase: Phase = .needsDisc
  @Published var log: [String] = []
  let paths = Paths()
  private var pendingDisc: DiscImage?

  init() { phase = paths.isInstalled ? .ready : .needsDisc }

  var busy: Bool {
    if case .working = phase { return true }
    return phase == .playing
  }

  // PATH for child processes: Apple's tools plus the usual Homebrew locations.
  private var environment: [String: String] {
    var env = ProcessInfo.processInfo.environment
    env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
    env["MODERNGEKKO_SOURCE_DIR"] = paths.toolchain.path
    return env
  }

  func append(_ text: String) {
    for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
      log.append(String(line))
    }
    if log.count > 400 { log.removeFirst(log.count - 400) }
  }

  // MARK: Disc intake

  func chooseDisc() {
    let panel = NSOpenPanel()
    panel.title = "Choose your Crazy Taxi (USA) disc image"
    panel.allowedContentTypes = ["iso", "ciso"].compactMap { UTType(filenameExtension: $0) }
    panel.allowsMultipleSelection = false
    if panel.runModal() == .OK, let url = panel.url { accept(url) }
  }

  func accept(_ url: URL) {
    guard !busy else { return }
    let ext = url.pathExtension.lowercased()
    guard ext == "iso" || ext == "ciso" else {
      phase = .failed("ReFare needs a .iso or .ciso disc image.")
      return
    }
    do {
      let disc = try DiscImage.inspect(url)
      pendingDisc = disc
      Task { await install(disc) }
    } catch {
      phase = .failed(error.localizedDescription)
    }
  }

  // MARK: Tools check

  private func hasTool(_ name: String) async -> Bool {
    await run("/usr/bin/which", [name], quiet: true) == 0
  }

  /// Recompiling needs Apple's Command Line Tools (clang, python3) plus CMake and Ninja.
  private func checkTools() async -> Bool {
    let clt = await run("/usr/bin/xcode-select", ["-p"], quiet: true) == 0
    let cmake = await hasTool("cmake")
    let ninja = await hasTool("ninja")
    if clt && cmake && ninja { return true }
    phase = .missingTools(clt: clt, cmake: cmake, ninja: ninja)
    return false
  }

  func retryInstall() {
    if let disc = pendingDisc { Task { await install(disc) } } else { phase = .needsDisc }
  }

  func installCommandLineTools() {
    Task { _ = await run("/usr/bin/xcode-select", ["--install"], quiet: true) }
  }

  // MARK: Install

  private func install(_ disc: DiscImage) async {
    guard await checkTools() else { return }
    let fm = FileManager.default
    log.removeAll()
    do {
      try fm.createDirectory(at: paths.support, withIntermediateDirectories: true)
      var iso = disc.url
      var temporaryISO: URL?
      if disc.isCISO {
        phase = .working("Converting the CISO disc image…")
        let target = paths.support.appendingPathComponent("converted.iso")
        try? fm.removeItem(at: target)
        try await Task.detached { try disc.expandCISO(to: target) }.value
        iso = target
        temporaryISO = target
      }
      defer { if let temp = temporaryISO { try? fm.removeItem(at: temp) } }

      phase = .working("Extracting the disc…")
      try? fm.removeItem(at: paths.extracted)
      try? fm.removeItem(at: paths.prepared)
      try fm.createDirectory(at: paths.extracted.deletingLastPathComponent(),
                             withIntermediateDirectories: true)
      try await step(paths.tool("dolrecomp").path, ["extract", iso.path, paths.extracted.path],
                     "Extracting the disc failed.")

      phase = .working("Preparing the game files…")
      try await step("/usr/bin/python3",
                     [paths.refare.appendingPathComponent("scripts/prepare-game.py").path,
                      paths.extracted.path, paths.prepared.path, "--skip-startup", "1"],
                     "Preparing the game files failed.")
      try? fm.removeItem(at: paths.extracted)  // the prepared copy is all that's needed

      phase = .working("Recompiling Crazy Taxi for your Mac. This takes a few minutes, once.")
      try await step(paths.tool("moderngekko-port").path,
                     ["build", paths.prepared.path, "--backend", "c", "--toolchain", "auto",
                      "--output", paths.modules.path],
                     "Recompiling failed.")

      try installDefaultSettings()
      pendingDisc = nil
      phase = .ready
    } catch {
      phase = .failed(error.localizedDescription)
    }
  }

  /// First run only: start from ReFare's recommended graphics settings.
  private func installDefaultSettings() throws {
    let fm = FileManager.default
    let config = paths.user.appendingPathComponent("Config")
    let gfx = config.appendingPathComponent("GFX.ini")
    guard !fm.fileExists(atPath: gfx.path) else { return }
    try fm.createDirectory(at: config, withIntermediateDirectories: true)
    try fm.copyItem(at: paths.refare.appendingPathComponent("config/GFX.ini"), to: gfx)
  }

  // MARK: Play

  func play() {
    guard phase == .ready else { return }
    phase = .playing
    Task {
      let status = await run(paths.tool("moderngekko-port").path,
                             ["run", paths.prepared.path, "--backend", "c", "--toolchain", "auto",
                              "--output", paths.modules.path, "--",
                              "--graphics", "Metal", "--user-dir", paths.user.path])
      phase = status == 0 ? .ready : .failed("The game closed with an error (\(status)). Details are in the log below.")
    }
  }

  func revealFiles() {
    NSWorkspace.shared.activateFileViewerSelecting([paths.support])
  }

  func changeDisc() {
    guard !busy else { return }
    phase = .needsDisc
  }

  // MARK: Processes

  struct StepError: LocalizedError {
    let message: String
    var errorDescription: String? { message + " Details are in the log below." }
  }

  private func step(_ path: String, _ args: [String], _ failure: String) async throws {
    let status = await run(path, args)
    if status != 0 { throw StepError(message: failure) }
  }

  /// Runs a tool, streaming its output into the log; returns the exit status.
  @discardableResult
  private func run(_ path: String, _ args: [String], quiet: Bool = false) async -> Int32 {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: path)
    process.arguments = args
    process.environment = environment
    process.currentDirectoryURL = paths.support
    let pipe = Pipe()
    process.standardOutput = pipe
    process.standardError = pipe
    if !quiet {
      pipe.fileHandleForReading.readabilityHandler = { handle in
        let data = handle.availableData
        guard !data.isEmpty else { return }
        let text = String(decoding: data, as: UTF8.self)
        Task { @MainActor in self.append(text) }
      }
    }
    do {
      try FileManager.default.createDirectory(at: paths.support, withIntermediateDirectories: true)
      try process.run()
    } catch {
      append("Couldn't start \(path): \(error.localizedDescription)")
      return -1
    }
    let status: Int32 = await withCheckedContinuation { continuation in
      DispatchQueue.global().async {
        process.waitUntilExit()
        continuation.resume(returning: process.terminationStatus)
      }
    }
    pipe.fileHandleForReading.readabilityHandler = nil
    return status
  }
}

// MARK: - Views

struct ContentView: View {
  @EnvironmentObject var model: LauncherModel
  @State private var dropTargeted = false
  @State private var showLog = false

  var body: some View {
    VStack(spacing: 18) {
      VStack(spacing: 4) {
        Text("ReFare").font(.system(size: 34, weight: .heavy, design: .rounded))
        Text("Crazy Taxi, recompiled for Mac").foregroundStyle(.secondary)
      }
      .padding(.top, 8)

      main
        .frame(maxWidth: .infinity, maxHeight: .infinity)

      DisclosureGroup("Log", isExpanded: $showLog) {
        ScrollViewReader { proxy in
          ScrollView {
            Text(model.log.joined(separator: "\n"))
              .font(.system(.caption, design: .monospaced))
              .frame(maxWidth: .infinity, alignment: .leading)
              .textSelection(.enabled)
              .id("end")
          }
          .frame(height: 110)
          .onChange(of: model.log.count) { _ in proxy.scrollTo("end", anchor: .bottom) }
        }
      }
      .font(.callout)
    }
    .padding(22)
    .frame(width: 520, height: 440)
    .dropDestination(for: URL.self) { urls, _ in
      guard let url = urls.first else { return false }
      model.accept(url)
      return true
    } isTargeted: { dropTargeted = $0 }
  }

  @ViewBuilder private var main: some View {
    switch model.phase {
    case .needsDisc:
      dropZone
    case .working(let message):
      VStack(spacing: 14) {
        ProgressView()
        Text(message).multilineTextAlignment(.center)
      }
    case .ready:
      VStack(spacing: 14) {
        Button(action: model.play) {
          Text("Play").font(.title2.weight(.bold)).frame(width: 180, height: 36)
        }
        .buttonStyle(.borderedProminent)
        .keyboardShortcut(.defaultAction)
        HStack(spacing: 16) {
          Button("Use a different disc…", action: model.changeDisc)
          Button("Show game files", action: model.revealFiles)
        }
        .buttonStyle(.link)
        .font(.callout)
      }
    case .playing:
      VStack(spacing: 10) {
        ProgressView()
        Text("Crazy Taxi is running. Quit the game to come back here.")
          .foregroundStyle(.secondary)
      }
    case .failed(let message):
      VStack(spacing: 12) {
        Image(systemName: "exclamationmark.triangle.fill").font(.largeTitle).foregroundStyle(.orange)
        Text(message).multilineTextAlignment(.center)
        Button("Try another disc image…", action: model.changeDisc)
      }
    case .missingTools(let clt, let cmake, let ninja):
      missingTools(clt: clt, cmake: cmake, ninja: ninja)
    }
  }

  private var dropZone: some View {
    VStack(spacing: 12) {
      Image(systemName: "opticaldisc").font(.system(size: 44)).foregroundStyle(.secondary)
      Text("Drop your Crazy Taxi (USA) disc image here")
        .font(.headline)
      Text(".iso or .ciso of the GameCube disc (GCTE51)")
        .font(.callout).foregroundStyle(.secondary)
      Button("Choose File…", action: model.chooseDisc)
        .padding(.top, 4)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .background(
      RoundedRectangle(cornerRadius: 14)
        .strokeBorder(style: StrokeStyle(lineWidth: 2, dash: [7, 5]))
        .foregroundStyle(dropTargeted ? Color.accentColor : Color.secondary.opacity(0.5))
    )
    .background(
      RoundedRectangle(cornerRadius: 14)
        .fill(dropTargeted ? Color.accentColor.opacity(0.08) : Color.clear)
    )
  }

  private func missingTools(clt: Bool, cmake: Bool, ninja: Bool) -> some View {
    VStack(alignment: .leading, spacing: 10) {
      Text("A few free developer tools are needed to recompile the game (one time):")
        .font(.headline)
      if !clt {
        HStack {
          Text("• Apple Command Line Tools")
          Spacer()
          Button("Install…", action: model.installCommandLineTools)
        }
      }
      if !cmake || !ninja {
        Text("• CMake and Ninja: install Homebrew from brew.sh, then run in Terminal:")
        Text("brew install cmake ninja")
          .font(.system(.body, design: .monospaced))
          .textSelection(.enabled)
          .padding(6)
          .background(RoundedRectangle(cornerRadius: 6).fill(Color.secondary.opacity(0.15)))
      }
      HStack {
        Spacer()
        Button("Check Again", action: model.retryInstall).buttonStyle(.borderedProminent)
      }
    }
  }
}

@main
struct ReFareLauncherApp: App {
  @StateObject private var model = LauncherModel()

  var body: some Scene {
    WindowGroup("ReFare") {
      ContentView().environmentObject(model)
    }
    .windowResizability(.contentSize)
    .commands { CommandGroup(replacing: .newItem) {} }
  }
}
