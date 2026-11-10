import Foundation
import CryptoKit

@main
struct ImportTests {
    static func require(_ condition: @autoclosure () -> Bool, _ reason: String) throws {
        if !condition() { throw DiscImportError.invalid("Test failed: " + reason) }
    }
    static func put(_ bytes: inout Data, _ offset: Int, _ value: UInt32) {
        bytes[offset] = UInt8(truncatingIfNeeded: value >> 24)
        bytes[offset + 1] = UInt8(truncatingIfNeeded: value >> 16)
        bytes[offset + 2] = UInt8(truncatingIfNeeded: value >> 8)
        bytes[offset + 3] = UInt8(truncatingIfNeeded: value)
    }
    static func main() throws {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("refare-import-test-" + UUID().uuidString)
        try fm.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: root) }
        var iso = Data(repeating: 0, count: 0x10000)
        iso.replaceSubrange(0..<6, with: Data("GCTE51".utf8))
        put(&iso, 0x1c, 0xC2339F3D)
        put(&iso, 0x420, 0x3000)
        put(&iso, 0x424, 0x4000)
        put(&iso, 0x428, 50)
        var dol = Data(repeating: 0, count: 0x104)
        put(&dol, 0, 0x100); put(&dol, 0x48, 0x80004000); put(&dol, 0x90, 4)
        put(&dol, 0x100, 0x12345678)
        iso.replaceSubrange(0x3000..<0x3104, with: dol)
        let hash = SHA256.hash(data: dol).map { String(format: "%02x", $0) }.joined()
        put(&iso, 0x4000, 0x01000000); put(&iso, 0x4008, 3)
        put(&iso, 0x400c, 0x01000000); put(&iso, 0x4014, 3)
        put(&iso, 0x4018, 4); put(&iso, 0x401c, 0x5000); put(&iso, 0x4020, 5)
        iso.replaceSubrange(0x4024..<0x4032, with: Data("dir\0hello.bin\0".utf8))
        iso.replaceSubrange(0x5000..<0x5005, with: Data("hello".utf8))
        let source = root.appendingPathComponent("fixture.iso")
        try iso.write(to: source)
        let destination = root.appendingPathComponent("extracted")
        let count = try DiscImporter.extractFromFileProvider(source, to: destination, expectedHash: hash)
        try require(count == 1, "file count")
        let payload = try Data(contentsOf: destination.appendingPathComponent("files/dir/hello.bin"))
        try require(payload == Data("hello".utf8), "nested file contents")
        let storedDOL = try Data(contentsOf: destination.appendingPathComponent("sys/main.dol"))
        try require(storedDOL == dol, "preserved DOL")
        do {
            try DiscImporter.extract(source, to: destination, expectedHash: hash)
            throw DiscImportError.invalid("Test failed: overwrote existing extraction")
        } catch DiscImportError.invalid(let reason) {
            try require(reason.contains("already imported"), "existing library protection")
        }
        do {
            try DiscImporter.extract(source, to: root.appendingPathComponent("wrong"))
            throw DiscImportError.invalid("Test failed: accepted wrong executable hash")
        } catch DiscImportError.invalid(let reason) {
            try require(reason.contains("revision"), "revision guard")
        }
        // CISO equivalent, followed by a missing logical block. No full ISO
        // expansion and no copyrighted data are involved in this fixture.
        var compressed = Data(repeating: 0, count: 0x8000)
        compressed.replaceSubrange(0..<4, with: Data("CISO".utf8))
        compressed[6] = 1 // little endian 0x10000-byte blocks
        compressed[8] = 1
        compressed.append(iso)
        let ciso = root.appendingPathComponent("fixture.ciso")
        try compressed.write(to: ciso)
        let cisoCount = try DiscImporter.extractFromFileProvider(ciso, to: root.appendingPathComponent("from-ciso"), expectedHash: hash)
        try require(cisoCount == 1, "CISO extraction")
        let gcm = root.appendingPathComponent("fixture.GCM")
        try iso.write(to: gcm)
        let gcmCount = try DiscImporter.extractFromFileProvider(gcm, to: root.appendingPathComponent("from-gcm"), expectedHash: hash)
        try require(gcmCount == 1, "uppercase GCM selection")
        let unsupported = root.appendingPathComponent("fixture.txt")
        try iso.write(to: unsupported)
        do {
            _ = try DiscImporter.extractFromFileProvider(unsupported, to: root.appendingPathComponent("unsupported"), expectedHash: hash)
            throw DiscImportError.invalid("Test failed: accepted unsupported selected file")
        } catch DiscImportError.invalid(let reason) {
            try require(reason.contains("Choose an ISO"), "unsupported selection feedback")
        }
        let directory = root.appendingPathComponent("folder.ciso")
        try fm.createDirectory(at: directory, withIntermediateDirectories: false)
        do {
            _ = try DiscImporter.extractFromFileProvider(directory, to: root.appendingPathComponent("directory"), expectedHash: hash)
            throw DiscImportError.invalid("Test failed: accepted selected folder")
        } catch DiscImportError.invalid(let reason) {
            try require(reason.contains("rather than a folder"), "folder selection feedback")
        }
        let reader = try DiscReader(ciso)
        let hole = try reader.read(0x10000, 16)
        try require(hole == Data(repeating: 0, count: 16), "CISO absent block zero fill")
        // Traversal is rejected and incomplete staging data removed.
        iso.replaceSubrange(0x4024..<0x4028, with: Data("..\0\0".utf8))
        let unsafe = root.appendingPathComponent("unsafe.iso")
        try iso.write(to: unsafe)
        let failed = root.appendingPathComponent("failed")
        do {
            try DiscImporter.extract(unsafe, to: failed, expectedHash: hash)
            throw DiscImportError.invalid("Test failed: accepted parent traversal")
        } catch DiscImportError.invalid(let reason) {
            try require(reason.contains("filename"), "path traversal guard")
        }
        try require(!fm.fileExists(atPath: failed.path), "failed extraction was committed")
        let remaining = try fm.contentsOfDirectory(atPath: root.path)
        try require(!remaining.contains { $0.hasPrefix(".import-") }, "staging cleanup")
        let listed = try DiscImporter.discs(in: root).map { $0.lastPathComponent }
        try require(listed.contains("fixture.ciso"), "folder fallback includes CISO")
        try require(listed.contains("fixture.GCM"), "folder fallback includes uppercase extensions")
        try require(!listed.contains("fixture.txt") && !listed.contains("folder.ciso"), "folder fallback excludes folders and unsupported files")
        print("PASS: coordinated ISO/CISO/GCM import, invalid file/folder selection feedback, nested files, original DOL preservation, revision guard, existing library protection, sparse blocks, traversal rejection, failure cleanup")
    }
}
