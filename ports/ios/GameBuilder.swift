import Foundation
import CryptoKit

final class GameBuilder {
    private let assets: URL
    private let libraryRoot: URL
    private let workRoot: URL
    private let documents: URL
    private let lock = NSLock()
    private var stopped = false
    init(assets: URL = Bundle.main.bundleURL.appendingPathComponent("BuilderAssets"),
         root: URL = GameBuilder.root,
         cache: URL = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0],
         documents: URL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]) {
        self.assets = assets; libraryRoot = root; workRoot = cache; self.documents = documents
    }
    func cancel() { lock.lock(); stopped = true; lock.unlock() }
    private func check() throws {
        lock.lock(); let canceled = stopped; lock.unlock()
        if canceled { throw CancellationError() }
    }
    private func invoke(_ arguments: [String], _ action: (Int32, UnsafePointer<UnsafePointer<CChar>?>?) -> Int32) throws {
        try check()
        let strings = arguments.map { strdup($0)! }
        defer { strings.forEach { free($0) } }
        let pointers = strings.map { Optional(UnsafePointer($0)) }
        let result = pointers.withUnsafeBufferPointer { action(Int32($0.count), $0.baseAddress) }
        guard result == 0 else { throw BuilderError.invalid("Compilation or linking failed (\(result)). Open the build log for details.") }
        try check()
    }
    static func sha256(_ url: URL) throws -> String {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        var hash = SHA256()
        while let block = try file.read(upToCount: 1024 * 1024), !block.isEmpty { hash.update(data: block) }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }
    static var root: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("ReFare")
    }
    static var log: URL { root.appendingPathComponent("builder.log") }

    func export(progress: (String) -> Void) throws -> URL {
        let fm = FileManager.default
        let tools = assets.appendingPathComponent("Toolchain")
        let game = libraryRoot.appendingPathComponent("GCTE51")
        let work = workRoot
            .appendingPathComponent("ReFareBuilder/build-" + UUID().uuidString)
        try fm.createDirectory(at: work, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: work) }
        try fm.createDirectory(at: libraryRoot, withIntermediateDirectories: true)
        ReFareBuilderLog(libraryRoot.appendingPathComponent("builder.log").path)
        let dol = work.appendingPathComponent("main.dol")
        try fm.copyItem(at: game.appendingPathComponent("sys/main.dol"), to: dol)
        guard ReFarePrepareDOL(dol.path, assets.appendingPathComponent("startup-patches.csv").path) == 0 else {
            throw BuilderError.invalid("Could not prepare your imported game. Open the build log for details.")
        }
        let toolID = try String(contentsOf: assets.appendingPathComponent("compiler-id.txt"), encoding: .utf8)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard toolID.count == 64, toolID.allSatisfy({ $0.isHexDigit }) else { throw BuilderError.invalid("Invalid compiler identity") }
        let key = try Self.sha256(dol) + "-" + toolID
        let cache = libraryRoot.appendingPathComponent("BuilderCache/" + key)
        let cachedFramework = cache.appendingPathComponent("ReFareGame.framework")
        let cachedBinary = cachedFramework.appendingPathComponent("ReFareGame")
        let receipt = cache.appendingPathComponent("module.sha256")
        try check()
        let cachedHash = try? String(contentsOf: receipt, encoding: .utf8)
        let actualHash = try? Self.sha256(cachedBinary)
        let validCache = cachedHash != nil && cachedHash == actualHash
        if !validCache {
            progress("Generating game code from your disc…")
            guard ReFareGenerate(dol.path, work.path) == 0 else { throw BuilderError.invalid("Game-code generation failed. Open the build log for details.") }
            try check()
            let generated = work.appendingPathComponent("generated")
            try fm.copyItem(at: dol, to: generated.appendingPathComponent("main.dol"))
            try ModuleTables.write(generated: generated, to: work.appendingPathComponent("module_tables.inc"))
            let chunks = try fm.contentsOfDirectory(at: generated.appendingPathComponent("chunks"), includingPropertiesForKeys: nil)
                .filter { $0.pathExtension == "c" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
            guard !chunks.isEmpty else { throw BuilderError.invalid("No game chunks were generated") }
            let sources = [tools.appendingPathComponent("module_export.c")] + chunks
            var objects: [String] = []
            for (index, source) in sources.enumerated() {
                try check()
                progress("Compiling \(index + 1) of \(sources.count). Keep ReFare open…")
                let object = work.appendingPathComponent("object-\(index).o")
                // Freestanding headers are ours/LLVM's. An Apple SDK is not distributed.
                try invoke(["clang", "--target=arm64-apple-ios15.0", "-std=c11", "-O2", "-c",
                    "-nostdinc", "-fPIC", "-fvisibility=hidden", "-fno-stack-protector",
                    "-DDOLRECOMP_CPU_HEADER=\"core/cpu.h\"", "-DMODULE_GAME_ID=\"GCTE51\"",
                    "-I" + tools.appendingPathComponent("cinclude").path,
                    "-I" + tools.appendingPathComponent("include").path,
                    "-I" + tools.appendingPathComponent("abi").path,
                    "-I" + work.path, "-I" + generated.path, source.path, "-o", object.path], ReFareCompile)
                objects.append(object.path)
            }
            progress("Linking the personal game framework…")
            let framework = work.appendingPathComponent("ReFareGame.framework")
            try fm.createDirectory(at: framework, withIntermediateDirectories: true)
            let binary = framework.appendingPathComponent("ReFareGame")
            try invoke(["ld64.lld", "-dylib", "-arch", "arm64", "-platform_version", "ios", "15.0", "27.1",
                "-install_name", "@rpath/ReFareGame.framework/ReFareGame", "-dead_strip", "-no_uuid",
                "-exported_symbol", "_staticrecomp_get_module", "-o", binary.path] + objects +
                [tools.appendingPathComponent("runtime-helpers.a").path, tools.appendingPathComponent("libSystem.tbd").path], ReFareLink)
            try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: binary.path)
            let info: [String: Any] = ["CFBundleExecutable": "ReFareGame", "CFBundleIdentifier": "io.github.bittahbandit1.refare.game",
                "CFBundleName": "ReFareGame", "CFBundlePackageType": "FMWK", "CFBundleVersion": "1",
                "CFBundleShortVersionString": "0.3.0", "MinimumOSVersion": "15.0",
                "CFBundleSupportedPlatforms": ["iPhoneOS"]]
            try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
                .write(to: framework.appendingPathComponent("Info.plist"), options: .atomic)
            try check()
            let pendingCache = work.appendingPathComponent("cache")
            try fm.createDirectory(at: pendingCache, withIntermediateDirectories: true)
            try fm.moveItem(at: framework, to: pendingCache.appendingPathComponent("ReFareGame.framework"))
            try Self.sha256(pendingCache.appendingPathComponent("ReFareGame.framework/ReFareGame"))
                .write(to: pendingCache.appendingPathComponent("module.sha256"), atomically: true, encoding: .utf8)
            if fm.fileExists(atPath: cache.path) { try fm.removeItem(at: cache) }
            try fm.createDirectory(at: cache.deletingLastPathComponent(), withIntermediateDirectories: true)
            try fm.moveItem(at: pendingCache, to: cache)
        } else { progress("Using your previously compiled game…") }
        try check()
        progress("Packaging your personal gameplay IPA…")
        let payload = work.appendingPathComponent("Payload")
        try fm.createDirectory(at: payload, withIntermediateDirectories: true)
        let app = payload.appendingPathComponent("ReFare.app")
        try IPAArchive.extractTemplate(assets.appendingPathComponent("GameplayTemplate.rfarchive"), to: app, check: check)
        let frameworks = app.appendingPathComponent("Frameworks")
        try fm.createDirectory(at: frameworks, withIntermediateDirectories: true)
        try fm.copyItem(at: cachedFramework, to: frameworks.appendingPathComponent("ReFareGame.framework"))
        // This private IPA carries the user's assets, so the second app can play
        // without importing the disc a second time. Never publish the result.
        try fm.copyItem(at: game, to: app.appendingPathComponent("PersonalGame"))
        try check()
        try fm.createDirectory(at: documents, withIntermediateDirectories: true)
        let output = documents.appendingPathComponent("ReFare-Personal-iOS15.ipa")
        try IPAArchive.write(payload: payload, to: output, check: check)
        return output
    }
}
