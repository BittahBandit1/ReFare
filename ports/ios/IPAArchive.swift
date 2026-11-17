import Foundation
import CryptoKit

enum BuilderError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { switch self { case .invalid(let message): return message } }
}

// Streaming, uncompressed ZIP writer. The exported IPA is passed to a signing
// tool; no installer or signing credentials are included in the builder.
enum IPAArchive {
    // The generic app template is bundled as data, not as a nested executable
    // app. It is only reconstructed inside the exported IPA's staging folder.
    static func extractTemplate(_ archive: URL, to destination: URL, check: () throws -> Void = {}) throws {
        let input = try FileHandle(forReadingFrom: archive)
        defer { try? input.close() }
        func read(_ count: Int) throws -> Data {
            let data = try input.read(upToCount: count) ?? Data()
            guard data.count == count else { throw BuilderError.invalid("Truncated gameplay template") }
            return data
        }
        func number(_ bytes: Int) throws -> UInt64 {
            try read(bytes).enumerated().reduce(0) { $0 | (UInt64($1.element) << ($1.offset * 8)) }
        }
        guard try read(8) == Data("RFTPL001".utf8) else { throw BuilderError.invalid("Invalid gameplay template") }
        let count = try number(4)
        guard count > 0, count <= 20000 else { throw BuilderError.invalid("Invalid gameplay template file count") }
        let fm = FileManager.default
        guard !fm.fileExists(atPath: destination.path) else { throw BuilderError.invalid("Template destination already exists") }
        try fm.createDirectory(at: destination, withIntermediateDirectories: true)
        var complete = false
        defer { if !complete { try? fm.removeItem(at: destination) } }
        var total: UInt64 = 0, names = Set<String>()
        for _ in 0..<count {
            try check()
            let length = try number(2), mode = try number(4), size = try number(8)
            guard length > 0, length <= 4096, size <= 1024 * 1024 * 1024, total + size <= 1024 * 1024 * 1024,
                  mode == 0o644 || mode == 0o755 else { throw BuilderError.invalid("Invalid gameplay template entry") }
            total += size
            let expected = try read(32)
            guard let name = String(data: try read(Int(length)), encoding: .utf8), !name.hasPrefix("/"),
                  !name.contains("\\"), !name.contains("\0"), !name.split(separator: "/", omittingEmptySubsequences: false).contains(where: { $0 == ".." || $0 == "." || $0.isEmpty }),
                  names.insert(name).inserted else { throw BuilderError.invalid("Invalid gameplay template path") }
            let output = destination.appendingPathComponent(name)
            try fm.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
            guard fm.createFile(atPath: output.path, contents: nil) else { throw BuilderError.invalid("Cannot unpack gameplay template") }
            let file = try FileHandle(forWritingTo: output)
            defer { try? file.close() }
            var remaining = size, hash = SHA256()
            while remaining > 0 {
                try check()
                let block = try read(Int(min(remaining, 1024 * 1024)))
                hash.update(data: block); try file.write(contentsOf: block)
                remaining -= UInt64(block.count)
            }
            guard Data(hash.finalize()) == expected else { throw BuilderError.invalid("Gameplay template checksum mismatch") }
            try file.close()
            try fm.setAttributes([.posixPermissions: NSNumber(value: mode)], ofItemAtPath: output.path)
        }
        guard (try input.read(upToCount: 1) ?? Data()).isEmpty else { throw BuilderError.invalid("Trailing gameplay template data") }
        complete = true
    }

    private static let crcTable: [UInt32] = (0..<256).map { value in
        var crc = UInt32(value)
        for _ in 0..<8 { crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320 : 0) }
        return crc
    }
    static func write(payload: URL, to destination: URL, check: () throws -> Void = {}) throws {
        let fm = FileManager.default
        let root = payload.standardizedFileURL.resolvingSymlinksInPath()
        let temporary = destination.appendingPathExtension("partial")
        try? fm.removeItem(at: temporary)
        guard fm.createFile(atPath: temporary.path, contents: nil) else { throw BuilderError.invalid("Cannot create IPA") }
        let file = try FileHandle(forWritingTo: temporary)
        defer { try? file.close(); try? fm.removeItem(at: temporary) }
        func emit(_ value: UInt64, _ bytes: Int) throws {
            try file.write(contentsOf: Data((0..<bytes).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) }))
        }
        struct Entry { let name: Data; let crc: UInt32; let size: UInt64; let offset: UInt64; let mode: UInt64 }
        var entries: [Entry] = []
        var names = Set<String>()
        guard let enumerator = fm.enumerator(at: root, includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey]) else {
            throw BuilderError.invalid("Missing IPA payload")
        }
        let files = enumerator.compactMap { $0 as? URL }.sorted { $0.path < $1.path }
        for source in files {
            try check()
            let attributes = try source.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            guard attributes.isSymbolicLink != true else { throw BuilderError.invalid("Symlinks are not allowed in the exported app") }
            guard attributes.isRegularFile == true else { continue }
            let prefix = root.path + "/"
            let sourcePath = source.standardizedFileURL.resolvingSymlinksInPath().path
            guard sourcePath.hasPrefix(prefix) else { throw BuilderError.invalid("File is outside the IPA payload") }
            let relative = String(sourcePath.dropFirst(prefix.count)).precomposedStringWithCanonicalMapping
            guard !relative.split(separator: "/").contains("..") else { throw BuilderError.invalid("Invalid IPA path") }
            guard names.insert(relative).inserted else { throw BuilderError.invalid("Duplicate IPA path") }
            let name = Data(("Payload/" + relative).utf8)
            guard name.count <= 65535, entries.count < 65535 else { throw BuilderError.invalid("IPA has too many files") }
            let offset = try file.offset()
            guard offset <= UInt32.max else { throw BuilderError.invalid("IPA exceeds 4 GB") }
            try emit(0x04034b50, 4); try emit(20, 2); try emit(0x808, 2)
            try emit(0, 2); try emit(0, 2); try emit(0x21, 2)
            try emit(0, 4); try emit(0, 4); try emit(0, 4)
            try emit(UInt64(name.count), 2); try emit(0, 2); try file.write(contentsOf: name)
            let input = try FileHandle(forReadingFrom: source)
            defer { try? input.close() }
            var crc: UInt32 = 0xffffffff, size: UInt64 = 0
            while let block = try input.read(upToCount: 1024 * 1024), !block.isEmpty {
                try check()
                size += UInt64(block.count)
                guard size <= UInt32.max else { throw BuilderError.invalid("IPA file exceeds 4 GB") }
                for byte in block { crc = (crc >> 8) ^ crcTable[Int((crc ^ UInt32(byte)) & 255)] }
                try file.write(contentsOf: block)
            }
            crc ^= 0xffffffff
            try emit(0x08074b50, 4); try emit(UInt64(crc), 4); try emit(size, 4); try emit(size, 4)
            let mode = (try fm.attributesOfItem(atPath: source.path)[.posixPermissions] as? NSNumber)?.uint64Value ?? 0o644
            entries.append(Entry(name: name, crc: crc, size: size, offset: offset, mode: 0o100000 | mode))
        }
        let central = try file.offset()
        for entry in entries {
            try emit(0x02014b50, 4); try emit(0x0314, 2); try emit(20, 2); try emit(0x808, 2)
            try emit(0, 2); try emit(0, 2); try emit(0x21, 2); try emit(UInt64(entry.crc), 4)
            try emit(entry.size, 4); try emit(entry.size, 4); try emit(UInt64(entry.name.count), 2)
            try emit(0, 2); try emit(0, 2); try emit(0, 2); try emit(0, 2)
            try emit(entry.mode << 16, 4); try emit(entry.offset, 4); try file.write(contentsOf: entry.name)
        }
        let end = try file.offset()
        guard end <= UInt32.max else { throw BuilderError.invalid("IPA exceeds 4 GB") }
        try emit(0x06054b50, 4); try emit(0, 2); try emit(0, 2)
        try emit(UInt64(entries.count), 2); try emit(UInt64(entries.count), 2)
        try emit(end - central, 4); try emit(central, 4); try emit(0, 2)
        try file.synchronize(); try file.close()
        try check()
        if fm.fileExists(atPath: destination.path) { _ = try fm.replaceItemAt(destination, withItemAt: temporary) }
        else { try fm.moveItem(at: temporary, to: destination) }
    }
}
