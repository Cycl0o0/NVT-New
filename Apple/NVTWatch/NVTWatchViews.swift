import SwiftUI

struct NVTWatchRootView: View {
    @EnvironmentObject private var store: NVTDashboardStore
    @State private var showingSettings = false

    private var theme: NVTNetworkTheme {
        .theme(for: store.selectedNetwork)
    }

    var body: some View {
        NavigationStack {
            List {
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("NVT")
                            .font(.title2.weight(.black))
                            .fontDesign(.rounded)
                            .foregroundStyle(theme.lineGradient)
                        Text(store.selectedNetwork.displayName)
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }

                    Picker("Network", selection: Binding(
                        get: { store.selectedNetwork },
                        set: { network in
                            Task {
                                await store.selectNetwork(network)
                            }
                        }
                    )) {
                        ForEach(store.supportedNetworks) { network in
                            Text(network.shortLabel).tag(network)
                        }
                    }

                    Button {
                        Task {
                            await store.reload()
                        }
                    } label: {
                        Label(store.isRefreshing ? "Refreshing..." : "Refresh", systemImage: "arrow.clockwise")
                    }

                    Button {
                        showingSettings = true
                    } label: {
                        Label("Backend URL", systemImage: "slider.horizontal.3")
                    }
                }

                Section("Lines") {
                    if store.filteredLines.isEmpty {
                        Text("No lines loaded yet.")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(store.filteredLines.prefix(18)) { line in
                            NavigationLink {
                                NVTWatchLineView(line: line)
                            } label: {
                                HStack {
                                    NVTLineBadge(line: line)
                                    Text(line.libelle)
                                        .lineLimit(2)
                                }
                            }
                        }
                    }
                }

                if !store.alerts.isEmpty {
                    Section("Alerts") {
                        ForEach(store.alerts.prefix(3), id: \.stableID) { alert in
                            VStack(alignment: .leading, spacing: 4) {
                                Text(alert.titre)
                                    .font(.headline)
                                    .lineLimit(2)
                                Text(alert.message)
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(3)
                            }
                        }
                    }
                }
            }
            .navigationTitle("NVT")
            .task {
                await store.bootstrap()
            }
            .sheet(isPresented: $showingSettings) {
                NVTWatchSettingsView(currentBaseURL: store.backendBaseURLString) { value in
                    await store.updateBackendURL(value)
                }
            }
        }
    }
}

struct NVTWatchLineView: View {
    @EnvironmentObject private var store: NVTDashboardStore
    let line: TransitLine

    var body: some View {
        List {
            Section {
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        NVTLineBadge(line: line)
                        Text(line.libelle)
                            .font(.headline.weight(.bold))
                            .lineLimit(2)
                    }

                    Text(line.vehicule.capitalized)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Stops") {
                if store.stops.isEmpty {
                    Text("Loading stops...")
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(store.filteredStops.prefix(18)) { stop in
                        NavigationLink {
                            NVTWatchPassagesView(line: line, stop: stop)
                        } label: {
                            VStack(alignment: .leading, spacing: 4) {
                                Text(stop.libelle)
                                    .font(.subheadline.weight(.semibold))
                                Text(stop.groupe)
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            }

            if !store.vehicles.isEmpty {
                Section("Vehicles") {
                    ForEach(store.vehicles.prefix(4)) { vehicle in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(vehicle.terminus)
                                .font(.subheadline.weight(.bold))
                            Text("\(vehicle.currentStopName) → \(vehicle.nextStopName)")
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            }

            if !store.alertsForSelectedLine.isEmpty {
                Section("Alerts") {
                    ForEach(store.alertsForSelectedLine.prefix(4), id: \.stableID) { alert in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(alert.titre)
                                .font(.subheadline.weight(.bold))
                            Text(alert.message)
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                                .lineLimit(3)
                        }
                    }
                }
            }
        }
        .navigationTitle(line.code)
        .task(id: line.gid) {
            await store.selectLine(line)
        }
    }
}

struct NVTWatchPassagesView: View {
    @EnvironmentObject private var store: NVTDashboardStore
    let line: TransitLine
    let stop: StopGroup

    var body: some View {
        List {
            Section {
                Text(stop.libelle)
                    .font(.headline.weight(.bold))
                Text(stop.groupe)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("Departures") {
                if store.passages.isEmpty {
                    Text("Loading departures...")
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(store.passages.prefix(8)) { passage in
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Text(passage.lineCode.isEmpty ? line.code : passage.lineCode)
                                    .font(.headline.weight(.black))
                                Spacer()
                                Text(passage.estimated.isEmpty ? passage.scheduled : passage.estimated)
                                    .font(.headline.monospacedDigit())
                            }
                            Text(passage.terminusName)
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                            if let waitingTime = passage.waitingTime, !waitingTime.isEmpty {
                                Text(waitingTime)
                                    .font(.caption2.weight(.semibold))
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            }
        }
        .navigationTitle("Passages")
        .task(id: stop.key) {
            await store.selectLine(line)
            await store.selectStop(stop)
        }
    }
}

struct NVTWatchSettingsView: View {
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
                TextField("Backend URL", text: $draftBaseURL)
                    .textInputAutocapitalization(.never)
                    .disableAutocorrection(true)

                Text("Use the full URL to the machine running `nvt-backend`, for example `http://192.168.1.20:8080`.")
                    .font(.caption2)
                    .foregroundStyle(.secondary)

                Button("Save") {
                    Task {
                        await onSave(draftBaseURL)
                        dismiss()
                    }
                }
            }
            .navigationTitle("Backend")
        }
    }
}
