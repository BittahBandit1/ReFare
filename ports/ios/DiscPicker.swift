import SwiftUI
import UIKit
import UniformTypeIdentifiers

// Some file providers report unknown disc formats as dynamic types that do
// not conform to public.data. Allow items and validate their contents after
// selection instead of silently disabling those files in the browser.
struct DiscPicker: UIViewControllerRepresentable {
    var selectFolder = false
    let selected: (URL) -> Void
    let cancelled: () -> Void

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: selectFolder ? [.folder] : [.item], asCopy: !selectFolder)
        picker.allowsMultipleSelection = false
        picker.shouldShowFileExtensions = true
        picker.delegate = context.coordinator
        return picker
    }

    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {
        context.coordinator.parent = self
    }

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        var parent: DiscPicker
        init(_ parent: DiscPicker) { self.parent = parent }

        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            guard let url = urls.first else { parent.cancelled(); return }
            parent.selected(url)
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            parent.cancelled()
        }
    }
}
