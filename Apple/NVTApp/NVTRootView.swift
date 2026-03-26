import SwiftUI

struct NVTRootView: View {
    @EnvironmentObject private var store: NVTDashboardStore
    @State private var showingSettings = false
    @Environment(\.colorScheme) private var colorScheme

    private var theme: NVTNetworkTheme {
        .theme(for: store.selectedNetwork)
    }

    private var metricColumns: [GridItem] {
        [GridItem(.adaptive(minimum: 160), spacing: 14)]
    }

    private var panelColumns: [GridItem] {
        [GridItem(.adaptive(minimum: 320), spacing: 18, alignment: .top)]
    }
    
    // Adaptive card background for dark/light mode
    private var cardBackground: some ShapeStyle {
        .regularMaterial
    }

    var body: some View {
        NavigationSplitView {
            sidebar
                .navigationTitle("NVT")
                .toolbar {
                    toolbarContent
                }
        } detail: {
            detail
                .toolbar {
                    toolbarContent
                }
        }
        .navigationSplitViewStyle(.balanced)
        .background(NVTSceneBackground())
        .task {
            await store.bootstrap()
        }
        .sheet(isPresented: $showingSettings) {
            settingsSheet
        }
    }

    @ViewBuilder
    private var settingsSheet: some View {
        let content = NVTSettingsView(currentBaseURL: store.backendBaseURLString) { value in
            await store.updateBackendURL(value)
        }

#if os(iOS) || os(visionOS)
        content.presentationDetents([.medium, .large])
#else
        content
#endif
    }

    private var sidebar: some View {
        ZStack {
            NVTSceneBackground()

            ScrollView(.vertical, showsIndicators: true) {
                VStack(alignment: .leading, spacing: 18) {
                    VStack(alignment: .leading, spacing: 16) {
                        VStack(alignment: .leading, spacing: 8) {
                            Text("NVT")
                                .font(.system(size: 42, weight: .black, design: .rounded))
                                .foregroundStyle(theme.lineGradient)
                            Text("A live control deck for your existing NVT backend, rebuilt as a native Apple app.")
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                        }

                        HStack {
                            Label(store.selectedNetwork.displayName, systemImage: theme.symbolName)
                                .font(.subheadline.weight(.bold))
                                .padding(.horizontal, 12)
                                .padding(.vertical, 8)
                                .background(theme.accent.opacity(0.12), in: Capsule())

                            Spacer()

                            Text(relativeTimestamp(store.lastUpdated))
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(.secondary)
                        }
                    }
                    .nvtGlassCard()

                    networkSwitcher
                    lineSearchCard
                    lineListCard
                    compactAlertsCard
                }
                .padding(20)
                .padding(.bottom, 20) // Extra bottom padding for scroll content
            }
            .scrollContentBackground(.hidden)
        }
    }

    private var networkSwitcher: some View {
        NVTSectionCard(title: "Networks", subtitle: "Switch the backend scope instantly.", tint: theme.accent) {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 120), spacing: 12)], spacing: 12) {
                ForEach(store.supportedNetworks, id: \.self) { network in
                    let active = network == store.selectedNetwork
                    let networkTheme = NVTNetworkTheme.theme(for: network)

                    Button {
                        Task {
                            await store.selectNetwork(network)
                        }
                    } label: {
                        HStack(spacing: 10) {
                            Image(systemName: networkTheme.symbolName)
                            Text(network.shortLabel)
                                .fontWeight(.bold)
                                .fontDesign(.rounded)
                            Spacer()
                        }
                        .padding(.horizontal, 14)
                        .padding(.vertical, 12)
                        .foregroundStyle(active ? Color.white : .primary)
                        .background(
                            RoundedRectangle(cornerRadius: 18, style: .continuous)
                                .fill(active ? AnyShapeStyle(networkTheme.accent) : AnyShapeStyle(.regularMaterial))
                        )
                    }
                    .buttonStyle(.plain)
                    .animation(.easeInOut(duration: 0.2), value: active)
                }
            }
        }
    }

    private var lineSearchCard: some View {
        NVTSectionCard(title: "Browse Lines", subtitle: "Search by line code, label, or mode.", tint: theme.secondary) {
            TextField("Search lines", text: $store.lineSearch)
                .textFieldStyle(.roundedBorder)
        }
    }

    private var lineListCard: some View {
        NVTSectionCard(title: "Lines", subtitle: "\(store.lines.count) live lines loaded.", tint: theme.accent) {
            if store.lines.isEmpty && store.isRefreshing {
                ProgressView("Loading lines...")
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else if store.filteredLines.isEmpty {
                NVTEmptyState(symbol: "tram", title: "No matching lines", message: "Try a broader search or switch to another network.")
            } else {
                LazyVStack(spacing: 10) {
                    ForEach(store.filteredLines.prefix(40)) { line in
                        let isSelected = line.gid == store.selectedLineID
                        Button {
                            Task {
                                await store.selectLine(line)
                            }
                        } label: {
                            HStack(spacing: 12) {
                                NVTLineBadge(line: line)
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(line.libelle)
                                        .font(.headline.weight(.bold))
                                        .foregroundStyle(.primary)
                                    Text(line.vehicule.capitalized)
                                        .font(.caption.weight(.semibold))
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                if line.active {
                                    NVTAlertPill(text: "LIVE", color: theme.accent)
                                }
                            }
                            .padding(14)
                            .background(
                                RoundedRectangle(cornerRadius: 20, style: .continuous)
                                    .fill(isSelected ? AnyShapeStyle(theme.accent.opacity(0.14)) : AnyShapeStyle(.regularMaterial))
                            )
                        }
                        .buttonStyle(.plain)
                        .animation(.easeInOut(duration: 0.15), value: isSelected)
                    }
                }
            }
        }
    }

    private var compactAlertsCard: some View {
        NVTSectionCard(title: "Live Alerts", subtitle: "The latest network disruptions.", tint: NVTColors.critical) {
            if store.alerts.isEmpty {
                NVTEmptyState(symbol: "checkmark.seal", title: "No live alerts", message: "This network currently reports no disruptions.")
            } else {
                LazyVStack(spacing: 12) {
                    ForEach(store.alerts.prefix(4), id: \.stableID) { alert in
                        VStack(alignment: .leading, spacing: 6) {
                            HStack {
                                Text(alert.titre)
                                    .font(.subheadline.weight(.bold))
                                    .lineLimit(2)
                                Spacer()
                                NVTAlertPill(text: alert.lineCode.isEmpty ? "Network" : alert.lineCode, color: severityColor(for: alert.severite))
                            }

                            Text(alert.message)
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                                .lineLimit(3)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(14)
                        .background(
                            RoundedRectangle(cornerRadius: 18, style: .continuous)
                                .fill(.regularMaterial)
                        )
                    }
                }
            }
        }
    }

    private var detail: some View {
        ZStack {
            NVTSceneBackground()

            ScrollView(.vertical, showsIndicators: true) {
                VStack(alignment: .leading, spacing: 22) {
                    heroPanel

                    if let errorMessage = store.errorMessage {
                        NVTStatusBanner(text: errorMessage, color: NVTColors.critical)
                    }

                    LazyVGrid(columns: metricColumns, spacing: 14) {
                        NVTMetricTile(
                            label: "Lines",
                            value: "\(store.lineStats?.total ?? store.lines.count)",
                            symbol: "point.3.connected.trianglepath.dotted",
                            tint: theme.accent
                        )
                        NVTMetricTile(
                            label: "Alerts",
                            value: "\(store.alertStats?.total ?? store.alerts.count)",
                            symbol: "bell.badge.fill",
                            tint: NVTColors.critical
                        )
                        NVTMetricTile(
                            label: "Stops",
                            value: "\(store.stops.count)",
                            symbol: "mappin.and.ellipse",
                            tint: theme.secondary
                        )
                        NVTMetricTile(
                            label: "Vehicles",
                            value: "\(store.vehicleStats?.total ?? store.vehicles.count)",
                            symbol: "location.north.line.fill",
                            tint: theme.highlight
                        )
                    }

                    NVTMapPanel(
                        network: store.selectedNetwork,
                        line: store.selectedLine,
                        theme: theme,
                        boundary: store.boundaryGeometry,
                        route: store.routeGeometry,
                        stops: store.filteredStops,
                        selectedStopKey: store.selectedStopKey,
                        vehicles: store.vehicles
                    )

                    LazyVGrid(columns: panelColumns, spacing: 18) {
                        stopPanel
                        passagePanel
                        vehiclePanel
                        alertPanel
                    }
                }
                .padding(22)
                .padding(.bottom, 20)
            }
            .scrollContentBackground(.hidden)
        }
    }

    private var heroPanel: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(alignment: .top, spacing: 16) {
                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 12) {
                        if let selectedLine = store.selectedLine {
                            NVTLineBadge(line: selectedLine)
                        }
                        Text(store.selectedLine?.libelle ?? store.selectedNetwork.displayName)
                            .font(.system(size: 34, weight: .black, design: .rounded))
                            .foregroundStyle(.primary)
                            .lineLimit(2)
                    }

                    Text(
                        store.selectedLine == nil
                            ? "Select a line to inspect live vehicles, stop passages, route geometry, and line-specific alerts."
                            : "Live detail for the selected line, fed directly by the local NVT backend."
                    )
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                }

                Spacer()

                VStack(alignment: .trailing, spacing: 8) {
                    Text(store.backendBaseURLString)
                        .font(.footnote.weight(.semibold))
                        .foregroundStyle(.secondary)
                    Label(store.backendInfo?.backend ?? "nvt-backend", systemImage: "network")
                        .font(.caption.weight(.bold))
                        .padding(.horizontal, 10)
                        .padding(.vertical, 6)
                        .background(theme.accent.opacity(0.12), in: Capsule())
                }
            }

            HStack(spacing: 12) {
                NVTAlertPill(text: store.selectedNetwork.displayName, color: theme.accent)
                if let line = store.selectedLine {
                    NVTAlertPill(text: line.vehicule.capitalized, color: theme.secondary)
                }
                if let alertCount = store.alertStats?.total, alertCount > 0 {
                    NVTAlertPill(text: "\(alertCount) alerts", color: NVTColors.critical)
                }
            }
        }
        .nvtGlassCard(padding: 22)
        .background(
            RoundedRectangle(cornerRadius: 30, style: .continuous)
                .fill(theme.lineGradient.opacity(0.15))
        )
    }

    private var stopPanel: some View {
        NVTSectionCard(
            title: "Stops",
            subtitle: store.selectedNetwork.requiresLineScopedStops ? "Line-scoped stop list." : "Search or tap a stop group.",
            tint: theme.secondary
        ) {
            VStack(alignment: .leading, spacing: 12) {
                TextField("Search stops", text: $store.stopSearch)
                    .textFieldStyle(.roundedBorder)

                if store.stops.isEmpty {
                    NVTEmptyState(symbol: "mappin.slash", title: "No stops loaded", message: "Select a line or refresh the backend connection.")
                } else {
                    LazyVStack(spacing: 10) {
                        ForEach(store.filteredStops.prefix(20)) { stop in
                            let isSelected = stop.key == store.selectedStopKey
                            Button {
                                Task {
                                    await store.selectStop(stop)
                                }
                            } label: {
                                HStack(alignment: .top, spacing: 12) {
                                    Image(systemName: isSelected ? "largecircle.fill.circle" : "circle")
                                        .foregroundStyle(isSelected ? theme.accent : .secondary)
                                    VStack(alignment: .leading, spacing: 4) {
                                        Text(stop.libelle)
                                            .font(.headline.weight(.bold))
                                            .foregroundStyle(.primary)
                                        Text(coordinateSummary(for: stop))
                                            .font(.caption)
                                            .foregroundStyle(.secondary)
                                    }
                                    Spacer()
                                }
                                .padding(14)
                                .background(
                                    RoundedRectangle(cornerRadius: 18, style: .continuous)
                                        .fill(isSelected ? AnyShapeStyle(theme.accent.opacity(0.12)) : AnyShapeStyle(.regularMaterial))
                                )
                            }
                            .buttonStyle(.plain)
                            .animation(.easeInOut(duration: 0.15), value: isSelected)
                        }
                    }
                }
            }
        }
    }

    private var passagePanel: some View {
        NVTSectionCard(
            title: "Next Passages",
            subtitle: store.selectedStop?.libelle ?? "Pick a stop to load departures.",
            tint: theme.highlight
        ) {
            if store.isLoadingPassages {
                ProgressView("Loading passages...")
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else if store.passages.isEmpty {
                NVTEmptyState(symbol: "clock.arrow.trianglehead.counterclockwise.rotate.90", title: "No departures yet", message: "Select a stop group to load live or scheduled passages.")
            } else {
                LazyVStack(spacing: 12) {
                    ForEach(store.passages.prefix(8)) { passage in
                        HStack(alignment: .top, spacing: 14) {
                            Circle()
                                .fill(Color(hex: passage.colorBg ?? "#0F766E"))
                                .frame(width: 12, height: 12)
                                .padding(.top, 5)

                            VStack(alignment: .leading, spacing: 6) {
                                HStack {
                                    Text(passage.lineCode.isEmpty ? "Line" : passage.lineCode)
                                        .font(.subheadline.weight(.black))
                                        .fontDesign(.rounded)
                                    Text(passage.terminusName)
                                        .font(.subheadline.weight(.semibold))
                                        .lineLimit(1)
                                    Spacer()
                                    Text(passage.estimated.isEmpty ? passage.scheduled : passage.estimated)
                                        .font(.title3.weight(.heavy))
                                        .monospacedDigit()
                                }
                                Text(passage.waitingTime ?? passage.stopName ?? passage.lineName)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)

                                HStack(spacing: 8) {
                                    if passage.live {
                                        NVTAlertPill(text: "REALTIME", color: theme.accent)
                                    }
                                    if passage.delayed {
                                        NVTAlertPill(text: "DELAYED", color: NVTColors.warning)
                                    }
                                }
                            }
                        }
                        .padding(14)
                        .background(
                            RoundedRectangle(cornerRadius: 20, style: .continuous)
                                .fill(.regularMaterial)
                        )
                    }
                }
            }
        }
    }

    private var vehiclePanel: some View {
        NVTSectionCard(
            title: "Vehicles",
            subtitle: store.selectedLine == nil ? "Choose a line first." : "Live motion and stop state.",
            tint: theme.accent
        ) {
            if store.isLoadingLineContext {
                ProgressView("Loading line context...")
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else if store.vehicles.isEmpty {
                NVTEmptyState(symbol: "bus.doubledecker", title: "No vehicle feed", message: "Some networks expose fewer live vehicle positions, but the rest of the line data stays available.")
            } else {
                LazyVStack(spacing: 12) {
                    ForEach(store.vehicles.prefix(8)) { vehicle in
                        VStack(alignment: .leading, spacing: 8) {
                            HStack {
                                Text(vehicle.terminus)
                                    .font(.headline.weight(.bold))
                                    .lineLimit(1)
                                Spacer()
                                NVTAlertPill(text: vehicle.sens.isEmpty ? "RUN" : vehicle.sens, color: vehicleToneColor(vehicle.tone))
                            }

                            Text("\(vehicle.currentStopName) -> \(vehicle.nextStopName)")
                                .font(.subheadline)
                                .foregroundStyle(.secondary)

                            HStack(spacing: 14) {
                                Label("\(vehicle.vitesse) km/h", systemImage: "speedometer")
                                if !vehicle.delayLabel.isEmpty {
                                    Label(vehicle.delayLabel, systemImage: "clock.badge.exclamationmark")
                                }
                                if vehicle.arret {
                                    Label("Stopped", systemImage: "pause.circle.fill")
                                }
                            }
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.secondary)
                        }
                        .padding(14)
                        .background(
                            RoundedRectangle(cornerRadius: 18, style: .continuous)
                                .fill(.regularMaterial)
                        )
                    }
                }
            }
        }
    }

    private var alertPanel: some View {
        NVTSectionCard(
            title: "Selected-Line Alerts",
            subtitle: store.selectedLine == nil ? "Network-level overview." : "Scoped to the current line when possible.",
            tint: NVTColors.critical
        ) {
            let scopedAlerts = store.alertsForSelectedLine

            if scopedAlerts.isEmpty {
                NVTEmptyState(symbol: "checkmark.shield", title: "No matching alerts", message: "The selected line currently has no disruption attached to it.")
            } else {
                LazyVStack(spacing: 12) {
                    ForEach(scopedAlerts.prefix(8), id: \.stableID) { alert in
                        VStack(alignment: .leading, spacing: 8) {
                            HStack {
                                Text(alert.titre)
                                    .font(.headline.weight(.bold))
                                    .lineLimit(2)
                                Spacer()
                                NVTAlertPill(
                                    text: alert.lineCode.isEmpty ? "NETWORK" : alert.lineCode,
                                    color: severityColor(for: alert.severite)
                                )
                            }

                            Text(alert.message)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)

                            if let scope = alert.scope, !scope.isEmpty {
                                Text(scope)
                                    .font(.caption.weight(.semibold))
                                    .foregroundStyle(.secondary)
                            }
                        }
                        .padding(14)
                        .background(
                            RoundedRectangle(cornerRadius: 18, style: .continuous)
                                .fill(.regularMaterial)
                        )
                    }
                }
            }
        }
    }

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItem(placement: .primaryAction) {
            Button {
                Task {
                    await store.reload()
                }
            } label: {
                if store.isRefreshing {
                    ProgressView()
                } else {
                    Label("Refresh", systemImage: "arrow.clockwise")
                }
            }
        }

        ToolbarItem(placement: .primaryAction) {
            Button {
                showingSettings = true
            } label: {
                Label("Settings", systemImage: "slider.horizontal.3")
            }
        }
    }
}
