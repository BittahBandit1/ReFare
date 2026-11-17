import Foundation

@main
enum BuilderExportIntegration {
    static func main() throws {
        let args = CommandLine.arguments
        let assets = URL(fileURLWithPath: args[1]), root = URL(fileURLWithPath: args[2])
        let output = URL(fileURLWithPath: args[3])
        let builder = GameBuilder(assets: assets, root: root, cache: root.appendingPathComponent("cache"), documents: output)
        var messages: [String] = []
        let ipa = try builder.export { message in
            messages.append(message)
            // Progress stays on stderr even when the native compiler redirects stdout.
            FileHandle.standardError.write(Data((message + "\n").utf8))
        }
        guard FileManager.default.fileExists(atPath: ipa.path) else { fatalError("IPA was not exported") }
        if args.count > 4 && args[4] == "cached" {
            guard messages.contains("Using your previously compiled game…"), !messages.contains(where: { $0.hasPrefix("Compiling ") }) else {
                fatalError("Expected an intact cache hit")
            }
        }
    }
}
