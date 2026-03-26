import SwiftUI

struct NVTSettingsView: View {
    @Environment(\.dismiss) private var dismiss

    @State private var draftBaseURL: String
    let onSave: (String) async -> Void

    init(currentBaseURL: String, onSave: @escaping (String) async -> Void) {
        _draftBaseURL = State(initialValue: currentBaseURL)
        self.onSave = onSave
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Backend") {
                    TextField("http://127.0.0.1:8080", text: $draftBaseURL)
#if os(iOS) || os(visionOS)
                        .textInputAutocapitalization(.never)
                        .keyboardType(.URL)
                        .disableAutocorrection(true)
#endif

                    Text("Use `127.0.0.1` on the Mac or simulator. On physical devices, point this to the machine running `nvt-backend` with its LAN IP.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                Section("Tip") {
                    Text("The app talks directly to the existing NVT backend over HTTP, so it picks up the same live lines, alerts, stops, passages, vehicles, and route geometry.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") {
                        dismiss()
                    }
                }

                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        Task {
                            await onSave(draftBaseURL)
                            dismiss()
                        }
                    }
                }
            }
        }
    }
}
