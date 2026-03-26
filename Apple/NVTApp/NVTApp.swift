import SwiftUI

@main
struct NVTApp: App {
    @StateObject private var store = NVTDashboardStore()

    var body: some Scene {
        WindowGroup("NVT") {
            NVTRootView()
                .environmentObject(store)
        }
#if os(macOS) || os(visionOS)
        .defaultSize(width: 1440, height: 940)
#endif
    }
}
