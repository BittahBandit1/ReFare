import Foundation

@main
enum BuilderTest {
    static func main() throws {
        let args = CommandLine.arguments
        switch args[1] {
        case "tables": try ModuleTables.write(generated: URL(fileURLWithPath: args[2]), to: URL(fileURLWithPath: args[3]))
        case "zip": try IPAArchive.write(payload: URL(fileURLWithPath: args[2]), to: URL(fileURLWithPath: args[3]))
        case "extract": try IPAArchive.extractTemplate(URL(fileURLWithPath: args[2]), to: URL(fileURLWithPath: args[3]))
        case "export":
            let root = URL(fileURLWithPath: args[3])
            let builder = GameBuilder(assets: URL(fileURLWithPath: args[2]), root: root,
                cache: root.appendingPathComponent("cache"), documents: URL(fileURLWithPath: args[4]))
            var cached = false
            _ = try builder.export { if $0 == "Using your previously compiled game…" { cached = true } }
            precondition(cached, "Fixture export must use its intact cache")
        case "cancel":
            var checks = 0
            do {
                try IPAArchive.write(payload: URL(fileURLWithPath: args[2]), to: URL(fileURLWithPath: args[3])) {
                    checks += 1
                    if checks > 1 { throw CancellationError() }
                }
                fatalError("Canceled export completed")
            } catch is CancellationError {}
        default: fatalError("Unknown test action")
        }
    }
}
