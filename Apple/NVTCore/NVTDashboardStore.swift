import Foundation

@MainActor
final class NVTDashboardStore: ObservableObject {
    @Published var supportedNetworks: [TransitNetwork]
    @Published var selectedNetwork: TransitNetwork
    @Published var selectedLineID: Int?
    @Published var selectedStopKey: String?
    @Published var lineSearch = ""
    @Published var stopSearch = ""
    @Published private(set) var backendInfo: BackendHealth?
    @Published private(set) var lines: [TransitLine] = []
    @Published private(set) var alerts: [ServiceAlert] = []
    @Published private(set) var stops: [StopGroup] = []
    @Published private(set) var passages: [Passage] = []
    @Published private(set) var vehicles: [Vehicle] = []
    @Published private(set) var boundaryGeometry: GeometryResponse?
    @Published private(set) var routeGeometry: GeometryResponse?
    @Published private(set) var lineStats: LineStats?
    @Published private(set) var alertStats: AlertStats?
    @Published private(set) var passageStats: PassageStats?
    @Published private(set) var vehicleStats: VehicleStats?
    @Published private(set) var isRefreshing = false
    @Published private(set) var isLoadingLineContext = false
    @Published private(set) var isLoadingPassages = false
    @Published private(set) var errorMessage: String?
    @Published private(set) var lastUpdated: Date?

    private let preferences: NVTPreferences
    private var hasBootstrapped = false

    init(preferences: NVTPreferences = NVTPreferences()) {
        self.preferences = preferences
        self.supportedNetworks = TransitNetwork.allCases
        self.selectedNetwork = preferences.selectedNetwork
        self.selectedLineID = preferences.selectedLineID
        self.selectedStopKey = preferences.selectedStopKey
    }

    var backendBaseURLString: String {
        preferences.normalizedBackendBaseURLString
    }

    private var client: NVTBackendClient {
        NVTBackendClient(baseURL: preferences.resolvedBaseURL)
    }

    var selectedLine: TransitLine? {
        guard let selectedLineID else { return nil }
        return lines.first(where: { $0.gid == selectedLineID })
    }

    var selectedStop: StopGroup? {
        guard let selectedStopKey else { return nil }
        return stops.first(where: { $0.key == selectedStopKey })
    }

    var filteredLines: [TransitLine] {
        guard !lineSearch.isEmpty else { return lines }
        let needle = lineSearch.lowercased()
        return lines.filter { $0.searchableText.contains(needle) }
    }

    var filteredStops: [StopGroup] {
        guard !stopSearch.isEmpty else { return stops }
        let needle = stopSearch.lowercased()
        return stops.filter { $0.searchableText.contains(needle) }
    }

    var alertsForSelectedLine: [ServiceAlert] {
        guard let selectedLine else { return alerts }
        return alerts.filter { alert in
            alert.ligneId == selectedLine.gid || alert.lineCode == selectedLine.code || (alert.lineCodes ?? []).contains(selectedLine.code)
        }
    }

    func bootstrap() async {
        guard !hasBootstrapped else { return }
        hasBootstrapped = true
        await reload(preserveSelection: true)
    }

    func reload(preserveSelection: Bool = true) async {
        isRefreshing = true
        errorMessage = nil

        do {
            var softFailure: String?
            let client = client

            let healthTask = Task { try await client.fetchHealth() }
            let health = try await healthTask.value
            backendInfo = health

            let networks = health.resolvedSupportedNetworks
            supportedNetworks = networks.isEmpty ? TransitNetwork.allCases : networks

            if !supportedNetworks.contains(selectedNetwork) {
                selectedNetwork = health.resolvedDefaultNetwork ?? supportedNetworks.first ?? .bordeaux
            }
            preferences.selectedNetwork = selectedNetwork

            let refreshedNetwork = selectedNetwork

            let linesTask = Task { try await client.fetchLines(network: refreshedNetwork) }
            let alertsTask = optionalTask { try await client.fetchAlerts(network: refreshedNetwork) }
            // Geometry improves the map, but it should not block the rest of the dashboard.
            let boundaryTask = refreshedNetwork.supportsBoundaries
                ? Task { try? await client.fetchMapBoundary(network: refreshedNetwork) }
                : nil
            let globalStopsTask = !refreshedNetwork.requiresLineScopedStops
                ? optionalTask { try await client.fetchStops(network: refreshedNetwork, lineID: nil) }
                : nil

            let linesResponse = try await linesTask.value
            let alertsResult = await alertsTask.value
            let boundaryResponse = await boundaryTask?.value
            let globalStopsResult = await globalStopsTask?.value

            lines = linesResponse.items.sorted(by: lineSort)
            lineStats = linesResponse.stats
            boundaryGeometry = boundaryResponse
            routeGeometry = nil
            vehicles = []
            vehicleStats = nil

            switch alertsResult {
            case .success(let alertsResponse):
                alerts = alertsResponse.items
                alertStats = alertsResponse.stats
            case .failure(let error):
                alerts = []
                alertStats = nil
                softFailure = softFailure ?? "Alerts unavailable: \(readableMessage(for: error))"
            }

            restoreSelectedLine(preserveSelection: preserveSelection)

            if let globalStopsResult {
                switch globalStopsResult {
                case .success(let globalStopsResponse):
                    applyStops(globalStopsResponse, preserveSelection: preserveSelection)
                case .failure(let error):
                    stops = []
                    passages = []
                    passageStats = nil
                    selectedStopKey = nil
                    preferences.selectedStopKey = nil
                    softFailure = softFailure ?? "Stops unavailable: \(readableMessage(for: error))"
                }
            } else {
                stops = []
                passages = []
                passageStats = nil
                if selectedNetwork.requiresLineScopedStops {
                    selectedStopKey = nil
                    preferences.selectedStopKey = nil
                }
            }

            if selectedLine != nil {
                await refreshLineContext(preserveStopSelection: preserveSelection)
            } else {
                passages = []
                passageStats = nil
            }

            lastUpdated = Date()
            if errorMessage == nil {
                errorMessage = softFailure
            }
        } catch {
            errorMessage = readableMessage(for: error)
        }

        isRefreshing = false
    }

    func selectNetwork(_ network: TransitNetwork) async {
        guard selectedNetwork != network else { return }
        selectedNetwork = network
        preferences.selectedNetwork = network
        selectedLineID = nil
        preferences.selectedLineID = nil
        selectedStopKey = nil
        preferences.selectedStopKey = nil
        await reload(preserveSelection: false)
    }

    func selectLine(_ line: TransitLine) async {
        guard selectedLineID != line.gid else { return }
        selectedLineID = line.gid
        preferences.selectedLineID = line.gid
        if selectedNetwork.requiresLineScopedStops {
            selectedStopKey = nil
            preferences.selectedStopKey = nil
            passages = []
            passageStats = nil
        }
        await refreshLineContext(preserveStopSelection: true)
    }

    func selectStop(_ stop: StopGroup) async {
        selectedStopKey = stop.key
        preferences.selectedStopKey = stop.key
        await loadPassages(for: stop)
    }

    func updateBackendURL(_ rawValue: String) async {
        preferences.backendBaseURL = rawValue
        hasBootstrapped = false
        await bootstrap()
    }

    private func refreshLineContext(preserveStopSelection: Bool) async {
        guard let selectedLine else { return }
        isLoadingLineContext = true
        errorMessage = nil

        var softFailure: String?
        let client = client
        let network = selectedNetwork
        let lineID = selectedLine.gid

        let vehiclesTask = optionalTask { try await client.fetchVehicles(network: network, lineID: lineID) }
        // Route geometry is optional; keep vehicles and stops usable if it is unavailable.
        let routeTask = network.supportsRoute
            ? Task { try? await client.fetchRoute(network: network, lineID: lineID) }
            : nil
        let scopedStopsTask = network.requiresLineScopedStops
            ? optionalTask { try await client.fetchStops(network: network, lineID: lineID) }
            : nil

        let vehiclesResult = await vehiclesTask.value
        let routeResponse = await routeTask?.value
        let scopedStopsResult = await scopedStopsTask?.value

        routeGeometry = routeResponse
        switch vehiclesResult {
        case .success(let vehiclesResponse):
            vehicles = vehiclesResponse.items
            vehicleStats = vehiclesResponse.stats
        case .failure(let error):
            vehicles = []
            vehicleStats = nil
            softFailure = softFailure ?? "Vehicles unavailable: \(readableMessage(for: error))"
        }

        if let scopedStopsResult {
            switch scopedStopsResult {
            case .success(let scopedStopsResponse):
                applyStops(scopedStopsResponse, preserveSelection: preserveStopSelection)
            case .failure(let error):
                stops = []
                passages = []
                passageStats = nil
                selectedStopKey = nil
                preferences.selectedStopKey = nil
                softFailure = softFailure ?? "Stops unavailable: \(readableMessage(for: error))"
            }
        }

        if let selectedStop {
            await loadPassages(for: selectedStop)
        } else {
            passages = []
            passageStats = nil
        }
        if errorMessage == nil {
            errorMessage = softFailure
        }

        isLoadingLineContext = false
    }

    private func loadPassages(for stop: StopGroup) async {
        isLoadingPassages = true
        errorMessage = nil

        do {
            let client = client
            let response = try await client.fetchPassages(
                network: selectedNetwork,
                stopKey: stop.key,
                lineID: selectedNetwork.requiresLineScopedStops ? selectedLine?.gid : nil
            )
            passages = response.items
            passageStats = response.stats
        } catch {
            errorMessage = readableMessage(for: error)
        }

        isLoadingPassages = false
    }

    private func applyStops(_ response: StopCollectionResponse, preserveSelection: Bool) {
        stops = response.items

        if preserveSelection, let selectedStopKey, stops.contains(where: { $0.key == selectedStopKey }) {
            self.selectedStopKey = selectedStopKey
        } else {
            self.selectedStopKey = stops.first?.key
        }

        preferences.selectedStopKey = self.selectedStopKey
    }

    private func restoreSelectedLine(preserveSelection: Bool) {
        if preserveSelection,
           let selectedLineID,
           lines.contains(where: { $0.gid == selectedLineID }) {
            self.selectedLineID = selectedLineID
            return
        }

        self.selectedLineID = lines.first(where: { $0.active })?.gid ?? lines.first?.gid
        preferences.selectedLineID = self.selectedLineID
    }

    private func lineSort(_ lhs: TransitLine, _ rhs: TransitLine) -> Bool {
        if lhs.active != rhs.active {
            return lhs.active && !rhs.active
        }

        if let left = Int(lhs.code), let right = Int(rhs.code), left != right {
            return left < right
        }

        if lhs.code != rhs.code {
            return lhs.code.localizedStandardCompare(rhs.code) == .orderedAscending
        }

        return lhs.libelle.localizedCaseInsensitiveCompare(rhs.libelle) == .orderedAscending
    }

    private func readableMessage(for error: Error) -> String {
        if let backend = error as? BackendFailure {
            return backend.message
        }
        return error.localizedDescription
    }

    private func optionalTask<T>(_ operation: @escaping @Sendable () async throws -> T) -> Task<Result<T, Error>, Never> {
        Task {
            do {
                return .success(try await operation())
            } catch {
                return .failure(error)
            }
        }
    }
}
