import SwiftUI

@main
struct NVTWatchApp: App {
    @StateObject private var store = NVTDashboardStore()

    var body: some Scene {
        WindowGroup {
            NVTWatchRootView()
                .environmentObject(store)
        }
    }
}
