import SwiftUI
import CoreText
#if os(macOS)
import AppKit
#else
import UIKit
#endif

enum ReFareBranding {
    static let yellow = Color(red: 1, green: 204.0 / 255, blue: 32.0 / 255)
    static let red = Color(red: 0.66, green: 0.065, blue: 0)
    static let ink = Color.black
    static func font(_ size: CGFloat) -> Font { .custom("ReFareUI", size: size) }
    static func registerFont() {
        if let url = Bundle.main.url(forResource: "ReFareUI", withExtension: "ttf") {
            CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
        }
    }
    static var logo: some View {
        Group {
#if os(macOS)
            if let url = Bundle.main.url(forResource: "ReFareLogo", withExtension: "png"), let image = NSImage(contentsOf: url) {
                Image(nsImage: image).resizable().scaledToFill()
            }
#else
            if let url = Bundle.main.url(forResource: "ReFareLogo", withExtension: "png"), let image = UIImage(contentsOfFile: url.path) {
                Image(uiImage: image).resizable().scaledToFill()
            }
#endif
        }.frame(width: 280, height: 140).clipped().accessibilityLabel("ReFare")
    }
    static var logoBar: some View {
        logo.frame(maxWidth: .infinity).frame(height: 160)
            .background(Color.white.ignoresSafeArea(edges: .horizontal))
    }
}
