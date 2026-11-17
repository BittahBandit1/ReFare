import Foundation

enum ModuleTables {
    struct Range: Equatable { let start: UInt64; let end: UInt64 }
    static func matches(_ pattern: String, _ text: String) throws -> [[String]] {
        let expression = try NSRegularExpression(pattern: pattern)
        return expression.matches(in: text, range: NSRange(text.startIndex..., in: text)).map { match in
            (1..<match.numberOfRanges).map { String(text[Swift.Range(match.range(at: $0), in: text)!]) }
        }
    }
    static func write(generated: URL, to output: URL) throws {
        let header = try String(contentsOf: generated.appendingPathComponent("generated.h"), encoding: .utf8)
        let hex: (String) -> UInt64 = { UInt64($0.replacingOccurrences(of: "0x", with: ""), radix: 16)! }
        var code: [UInt64: UInt64] = [:]
        for pair in try matches("address >= (0x[0-9A-Fa-f]+)u && address < (0x[0-9A-Fa-f]+)u", header) {
            code[hex(pair[0])] = hex(pair[1])
        }
        for pair in try matches(#"u32\s+offset\s*=\s*address\s*-\s*(0x[0-9A-Fa-f]+)u\s*;\s*if\s*\(\s*offset\s*<\s*(0x[0-9A-Fa-f]+)u"#, header) {
            code[hex(pair[0])] = hex(pair[0]) + hex(pair[1])
        }
        let coverage = code.keys.sorted().map { Range(start: $0, end: code[$0]!) }
        let starts = Set(try matches(#"void func_([0-9A-Fa-f]{8})\(CPUState\* ctx\);"#, header).map { hex($0[0]) }).sorted()
        guard !coverage.isEmpty, !starts.isEmpty else { throw BuilderError.invalid("No generated executable coverage") }
        let chunks = try starts.enumerated().map { index, start -> Range in
            guard let section = coverage.first(where: { $0.start <= start && start < $0.end }) else {
                throw BuilderError.invalid("Generated chunk is outside executable coverage")
            }
            let next = index + 1 < starts.count ? starts[index + 1] : section.end
            return Range(start: start, end: min(next, section.end))
        }
        let smcText = try String(contentsOf: generated.appendingPathComponent("generated_smc.txt"), encoding: .utf8)
        let smc = try matches(#"(?m)^\s*(0x[0-9A-Fa-f]+)-(0x[0-9A-Fa-f]+)"#, smcText)
            .map { Range(start: hex($0[0]), end: hex($0[1]) + 4) }.sorted { $0.start < $1.start }
        let dol = try Data(contentsOf: generated.appendingPathComponent("main.dol"))
        guard dol.count >= 0x100 else { throw BuilderError.invalid("Truncated DOL header") }
        func word(_ offset: Int) -> UInt64 {
            dol[offset..<offset + 4].reduce(0) { ($0 << 8) | UInt64($1) }
        }
        func hash(_ chunk: Range) throws -> UInt64 {
            for index in 0..<18 {
                let file = word(index * 4), base = word(0x48 + index * 4), size = word(0x90 + index * 4)
                if file != 0 && base <= chunk.start && chunk.end <= base + size {
                    let start = file + chunk.start - base, count = chunk.end - chunk.start
                    guard start <= UInt64(dol.count), count <= UInt64(dol.count) - start else {
                        throw BuilderError.invalid("Truncated DOL section")
                    }
                    return dol[Int(start)..<Int(start + count)].reduce(0xcbf29ce484222325) { ($0 ^ UInt64($1)) &* 0x100000001b3 }
                }
            }
            throw BuilderError.invalid("Chunk is not contained in a DOL section")
        }
        func table(_ name: String, _ count: String, _ values: [Range]) -> String {
            let rows = values.isEmpty ? "    {0u, 0u},\n" : values.map {
                String(format: "    {0x%08llXu, 0x%08llXu},\n", $0.start, $0.end)
            }.joined()
            return "static const StaticRecompRange \(name)[] = {\n\(rows)};\n#define \(count) \(values.count)u\n"
        }
        var result = "// Generated locally from the imported disc.\n"
        result += table("s_code_ranges", "MODULE_CODE_RANGE_COUNT", coverage)
        result += table("s_smc_ranges", "MODULE_SMC_RANGE_COUNT", smc)
        result += table("s_chunk_ranges", "MODULE_CHUNK_RANGE_COUNT", chunks)
        result += "static const u64 s_chunk_hashes[] = {\n"
        for chunk in chunks { result += String(format: "    0x%016llXu,\n", try hash(chunk)) }
        result += "};\n"
        try result.write(to: output, atomically: true, encoding: .utf8)
    }
}
