// Run on macOS: swift scripts/generate-icons.swift
// All platforms build from the checked-in results; no image tools are needed
// on Windows/Linux. The original artwork is kept byte-for-byte in assets/.
import Foundation
import CoreGraphics
import ImageIO

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let source = root.appendingPathComponent("assets/ReFareIcon.png")
let output = root.appendingPathComponent("assets/icons")
let fm = FileManager.default
try fm.createDirectory(at: output, withIntermediateDirectories: true)
guard let input = CGImageSourceCreateWithURL(source as CFURL, nil),
      let original = CGImageSourceCreateImageAtIndex(input, 0, nil), original.width == original.height else {
    fatalError("The canonical app icon must be a square image")
}
var images: [Int: Data] = [:]
func render(_ size: Int, type: String = "public.png", image: CGImage? = nil) -> Data {
    let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
        bytesPerRow: size * 4, space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    context.setFillColor(image == nil ? CGColor(red: 1, green: 0.82, blue: 0.125, alpha: 1) : CGColor(gray: 1, alpha: 1))
    context.fill(CGRect(x: 0, y: 0, width: size, height: size))
    context.interpolationQuality = .high
    context.draw(image ?? original, in: CGRect(x: 0, y: 0, width: size, height: size))
    let data = NSMutableData()
    let destination = CGImageDestinationCreateWithData(data, type as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, context.makeImage()!, nil)
    precondition(CGImageDestinationFinalize(destination))
    return data as Data
}
for size in [16, 20, 29, 32, 40, 48, 58, 60, 64, 72, 76, 80, 87, 96, 120, 128, 144, 152, 160, 167, 180, 192, 256, 512, 1024] {
    let data = render(size)
    images[size] = data
    try data.write(to: output.appendingPathComponent("icon-\(size).png"))
}
try render(256, type: "com.microsoft.bmp").write(to: output.appendingPathComponent("ReFare.bmp"))
let logoSource = CGImageSourceCreateWithURL(root.appendingPathComponent("assets/branding/ReFareLogo.png") as CFURL, nil)!
let launcherLogo = CGImageSourceCreateImageAtIndex(logoSource, 0, nil)!
try render(512, type: "com.microsoft.bmp", image: launcherLogo).write(to: root.appendingPathComponent("assets/branding/ReFareLogo.bmp"))
func little(_ value: Int, _ count: Int) -> Data {
    Data((0..<count).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
}
let sizes = [16, 32, 48, 64, 128, 256]
var ico = little(0, 2) + little(1, 2) + little(sizes.count, 2)
var offset = 6 + sizes.count * 16
for size in sizes {
    let data = images[size]!
    ico += little(size == 256 ? 0 : size, 1) + little(size == 256 ? 0 : size, 1)
    ico += little(0, 2) + little(1, 2) + little(32, 2) + little(data.count, 4) + little(offset, 4)
    offset += data.count
}
for size in sizes { ico += images[size]! }
try ico.write(to: output.appendingPathComponent("ReFare.ico"))
let iconset = root.appendingPathComponent("build/icons/ReFare.iconset")
try fm.createDirectory(at: iconset, withIntermediateDirectories: true)
for size in [16, 32, 128, 256, 512] {
    try images[size]!.write(to: iconset.appendingPathComponent("icon_\(size)x\(size).png"))
    try images[size * 2]!.write(to: iconset.appendingPathComponent("icon_\(size)x\(size)@2x.png"))
}
func big(_ value: Int) -> Data { Data((0..<4).reversed().map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) }) }
var chunks = Data()
for (type, size) in [("icp4",16), ("icp5",32), ("icp6",64), ("ic07",128), ("ic08",256), ("ic09",512), ("ic10",1024)] {
    let png = images[size]!
    chunks += Data(type.utf8) + big(png.count + 8) + png
}
try (Data("icns".utf8) + big(chunks.count + 8) + chunks).write(to: output.appendingPathComponent("ReFare.icns"))
print("Generated ReFare icons from assets/ReFareIcon.png")
