import Foundation
import CryptoKit

enum DiscImportError: LocalizedError {
    case invalid(String)
    var errorDescription: String? {
        switch self { case .invalid(let reason): return reason }
    }
}

// A bounded random-access reader, including GameCube block-map CISO. Missing
// CISO blocks read as zeroes; no temporary 1.36 GiB ISO is needed on the phone.
final class DiscReader {
    private let file: FileHandle
    private let physicalSize: UInt64
    private var blockSize: UInt64 = 0
    private var blockOffsets: [UInt64?] = []
    private(set) var logicalSize: UInt64

    init(_ url: URL) throws {
        file = try FileHandle(forReadingFrom: url)
        physicalSize = try file.seekToEnd()
        logicalSize = physicalSize
        let signature = try physicalRead(0, 8)
        if signature.prefix(4) == Data("CISO".utf8) {
            let header = try physicalRead(0, 0x8000)
            blockSize = UInt64(header[4]) | UInt64(header[5]) << 8 |
                        UInt64(header[6]) << 16 | UInt64(header[7]) << 24
            guard blockSize >= 0x440, blockSize <= 32 * 1024 * 1024 else {
                throw DiscImportError.invalid("Invalid GameCube CISO block size.")
            }
            var next: UInt64 = 0x8000
            for present in header.dropFirst(8) {
                guard present <= 1 else { throw DiscImportError.invalid("Invalid CISO block map.") }
                blockOffsets.append(present == 1 ? next : nil)
                if present == 1 { next += blockSize }
            }
            guard next <= physicalSize, blockOffsets[0] != nil else {
                throw DiscImportError.invalid("The CISO is truncated or has no disc header.")
            }
            logicalSize = min(1_459_978_240, UInt64(blockOffsets.count) * blockSize)
        }
    }

    deinit { try? file.close() }

    private func physicalRead(_ offset: UInt64, _ count: Int) throws -> Data {
        guard count >= 0, offset <= physicalSize, UInt64(count) <= physicalSize - offset else {
            throw DiscImportError.invalid("The disc image is truncated.")
        }
        try file.seek(toOffset: offset)
        let bytes = try file.read(upToCount: count) ?? Data()
        guard bytes.count == count else { throw DiscImportError.invalid("Could not read the disc image.") }
        return bytes
    }

    func read(_ offset: UInt64, _ count: Int) throws -> Data {
        guard count >= 0, offset <= logicalSize, UInt64(count) <= logicalSize - offset else {
            throw DiscImportError.invalid("A disc entry lies outside the image.")
        }
        if blockSize == 0 { return try physicalRead(offset, count) }
        var bytes = Data()
        var position = offset
        while bytes.count < count {
            let block = Int(position / blockSize)
            let within = position % blockSize
            let amount = min(count - bytes.count, Int(blockSize - within))
            if let physical = blockOffsets[block] {
                bytes.append(try physicalRead(physical + within, amount))
            } else { bytes.append(Data(repeating: 0, count: amount)) }
            position += UInt64(amount)
        }
        return bytes
    }
}

enum DiscImporter {
    static func discs(in folder: URL) throws -> [URL] {
        return try FileManager.default.contentsOfDirectory(at: folder,
            includingPropertiesForKeys: [.isRegularFileKey], options: [.skipsHiddenFiles])
            .filter { url in
                ["iso", "gcm", "ciso"].contains(url.pathExtension.lowercased()) &&
                    (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true
            }
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
    }

    static let dolSHA256 = "ca0e38f6b7d481fc512e4d67c8704261ce95a01ccc6a8680d963f6b8edd494d7"
    // The caller owns security-scoped access. Coordination asks a document
    // provider to materialize its file and prevents a move/write during reads.
    static func extractFromFileProvider(_ source: URL, to destination: URL,
                                       expectedHash: String = dolSHA256) throws -> Int {
        guard ["iso", "gcm", "ciso"].contains(source.pathExtension.lowercased()) else {
            throw DiscImportError.invalid("Choose an ISO, GCM or GameCube CISO disc image.")
        }
        let coordinator = NSFileCoordinator(filePresenter: nil)
        var coordinationError: NSError?
        var result: Result<Int, Error>?
        coordinator.coordinate(readingItemAt: source, options: [], error: &coordinationError) { url in
            result = Result {
                let values = try url.resourceValues(forKeys: [.isRegularFileKey])
                guard values.isRegularFile == true else {
                    throw DiscImportError.invalid("Choose a disc image file, rather than a folder.")
                }
                return try extract(url, to: destination, expectedHash: expectedHash)
            }
        }
        if let error = coordinationError {
            throw DiscImportError.invalid("The file provider could not open this disc: \(error.localizedDescription) Try downloading it in Files first, then choose it again.")
        }
        guard let result = result else {
            throw DiscImportError.invalid("The file provider did not return a readable file. Download it in Files and try again.")
        }
        return try result.get()
    }
    static func word(_ bytes: Data, _ offset: Int) throws -> UInt32 {
        guard offset >= 0, offset + 4 <= bytes.count else {
            throw DiscImportError.invalid("Truncated disc structure.")
        }
        return UInt32(bytes[offset]) << 24 | UInt32(bytes[offset + 1]) << 16 |
               UInt32(bytes[offset + 2]) << 8 | UInt32(bytes[offset + 3])
    }

    @discardableResult
    static func extract(_ source: URL, to destination: URL,
                        expectedHash: String = dolSHA256) throws -> Int {
        let fm = FileManager.default
        guard !fm.fileExists(atPath: destination.path) else {
            throw DiscImportError.invalid("A game is already imported. Existing game files were kept.")
        }
        let disc = try DiscReader(source)
        let boot = try disc.read(0, 0x440)
        guard boot.prefix(6) == Data("GCTE51".utf8), boot[6] == 0, boot[7] == 0,
              try word(boot, 0x1c) == 0xC2339F3D else {
            throw DiscImportError.invalid("Use Crazy Taxi USA (GCTE51), disc 1, revision 0.")
        }
        let dolOffset = UInt64(try word(boot, 0x420))
        let dolHeader = try disc.read(dolOffset, 0x100)
        var dolSize: UInt64 = 0x100
        for i in 0..<18 {
            let start = UInt64(try word(dolHeader, i * 4))
            let size = UInt64(try word(dolHeader, 0x90 + i * 4))
            if size > 0 { dolSize = max(dolSize, start + size) }
        }
        guard dolSize <= 64 * 1024 * 1024 else { throw DiscImportError.invalid("Invalid DOL size.") }
        let dol = try disc.read(dolOffset, Int(dolSize))
        let hash = SHA256.hash(data: dol).map { String(format: "%02x", $0) }.joined()
        guard hash == expectedHash else { throw DiscImportError.invalid("Unsupported game executable revision.") }
        let fstOffset = UInt64(try word(boot, 0x424))
        let fstSize = Int(try word(boot, 0x428))
        guard fstSize >= 12, fstSize <= 32 * 1024 * 1024 else {
            throw DiscImportError.invalid("Invalid disc filesystem size.")
        }
        let fst = try disc.read(fstOffset, fstSize)
        let entries = Int(try word(fst, 8))
        guard try word(fst, 0) >> 24 == 1, entries >= 1, entries <= fstSize / 12 else {
            throw DiscImportError.invalid("Invalid disc filesystem entries.")
        }

        // Commit only a fully verified extraction. Every failure removes its
        // staging directory; an existing library is never overwritten.
        try fm.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        let stage = destination.deletingLastPathComponent().appendingPathComponent(".import-" + UUID().uuidString)
        try fm.createDirectory(at: stage.appendingPathComponent("sys"), withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: stage) }
        let files = stage.appendingPathComponent("files", isDirectory: true)
        try fm.createDirectory(at: files, withIntermediateDirectories: true)
        var stack: [(end: Int, url: URL)] = [(entries, files)]
        var count = 0
        for index in 1..<entries {
            while let last = stack.last, index >= last.end { stack.removeLast() }
            guard let parent = stack.last else { throw DiscImportError.invalid("Invalid directory nesting.") }
            let entry = index * 12
            let tag = try word(fst, entry)
            let nameOffset = entries * 12 + Int(tag & 0xFFFFFF)
            guard nameOffset < fst.count,
                  let end = fst[nameOffset...].firstIndex(of: 0),
                  let name = String(data: fst[nameOffset..<end], encoding: .utf8),
                  !name.isEmpty, name != ".", name != "..",
                  !name.contains("/"), !name.contains("\\"), !name.contains(":") else {
                throw DiscImportError.invalid("Invalid filename in disc filesystem.")
            }
            let output = parent.url.appendingPathComponent(name)
            guard !fm.fileExists(atPath: output.path) else {
                throw DiscImportError.invalid("Duplicate filename in disc filesystem.")
            }
            if tag >> 24 == 1 {
                let end = Int(try word(fst, entry + 8))
                guard end > index, end <= parent.end,
                      stack.count < 128 else { throw DiscImportError.invalid("Invalid directory range.") }
                try fm.createDirectory(at: output, withIntermediateDirectories: false)
                stack.append((end, output))
            } else {
                guard tag >> 24 == 0 else { throw DiscImportError.invalid("Invalid filesystem entry type.") }
                let offset = UInt64(try word(fst, entry + 4))
                let size = UInt64(try word(fst, entry + 8))
                guard offset <= disc.logicalSize, size <= disc.logicalSize - offset else {
                    throw DiscImportError.invalid("File lies outside disc image.")
                }
                guard fm.createFile(atPath: output.path, contents: nil) else {
                    throw DiscImportError.invalid("Cannot create imported file.")
                }
                let handle = try FileHandle(forWritingTo: output)
                defer { try? handle.close() }
                var copied: UInt64 = 0
                while copied < size {
                    let amount = Int(min(1024 * 1024, size - copied))
                    try handle.write(contentsOf: disc.read(offset + copied, amount))
                    copied += UInt64(amount)
                }
                count += 1
            }
        }
        let system = stage.appendingPathComponent("sys")
        try boot.write(to: system.appendingPathComponent("boot.bin"))
        try disc.read(0x440, 0x2000).write(to: system.appendingPathComponent("bi2.bin"))
        let loader = try disc.read(0x2440, 0x20)
        let loaderSize = UInt64(try word(loader, 0x14)) + UInt64(try word(loader, 0x18)) + 0x20
        guard loaderSize <= 16 * 1024 * 1024 else { throw DiscImportError.invalid("Invalid apploader size.") }
        try disc.read(0x2440, Int(loaderSize)).write(to: system.appendingPathComponent("apploader.img"))
        try fst.write(to: system.appendingPathComponent("fst.bin"))
        try dol.write(to: system.appendingPathComponent("main.dol"))
        try fm.moveItem(at: stage, to: destination)
        return count
    }
}
