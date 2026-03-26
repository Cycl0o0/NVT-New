import Foundation

struct BackendFailure: LocalizedError {
    let code: String?
    let statusCode: Int?
    let message: String

    var errorDescription: String? {
        message
    }
}

struct NVTBackendClient: Sendable {
    let baseURL: URL?

    func fetchIndex() async throws -> BackendIndex {
        try await request(path: "/")
    }

    func fetchHealth() async throws -> BackendHealth {
        try await request(path: "/api/health")
    }

    func fetchLines(network: TransitNetwork) async throws -> LineCollectionResponse {
        try await request(path: "/api/lines", query: [
            URLQueryItem(name: "network", value: network.queryValue),
        ])
    }

    func fetchAlerts(network: TransitNetwork) async throws -> AlertCollectionResponse {
        try await request(path: "/api/alerts", query: [
            URLQueryItem(name: "network", value: network.queryValue),
        ])
    }

    func fetchStops(network: TransitNetwork, lineID: Int?) async throws -> StopCollectionResponse {
        var query = [URLQueryItem(name: "network", value: network.queryValue)]
        if let lineID {
            query.append(URLQueryItem(name: "line", value: String(lineID)))
        }
        return try await request(path: "/api/stop-groups", query: query)
    }

    func fetchPassages(network: TransitNetwork, stopKey: String, lineID: Int?) async throws -> PassageCollectionResponse {
        var query = [URLQueryItem(name: "network", value: network.queryValue)]
        if let lineID {
            query.append(URLQueryItem(name: "line", value: String(lineID)))
        }
        return try await request(path: "/api/stop-groups/\(stopKey)/passages", query: query)
    }

    func fetchVehicles(network: TransitNetwork, lineID: Int) async throws -> VehicleCollectionResponse {
        try await request(path: "/api/lines/\(lineID)/vehicles", query: [
            URLQueryItem(name: "network", value: network.queryValue),
        ])
    }

    func fetchMapBoundary(network: TransitNetwork) async throws -> GeometryResponse {
        try await request(path: "/api/map/boundaries", query: [
            URLQueryItem(name: "network", value: network.queryValue),
        ])
    }

    func fetchRoute(network: TransitNetwork, lineID: Int) async throws -> GeometryResponse {
        try await request(path: "/api/lines/\(lineID)/route", query: [
            URLQueryItem(name: "network", value: network.queryValue),
        ])
    }

    private func request<T: Decodable>(path: String, query: [URLQueryItem] = []) async throws -> T {
        let request = try buildRequest(path: path, query: query)
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse else {
            throw BackendFailure(code: nil, statusCode: nil, message: "The backend returned an invalid response.")
        }

        guard (200..<300).contains(httpResponse.statusCode) else {
            let decoder = JSONDecoder()
            if let payload = try? decoder.decode(BackendErrorPayload.self, from: data) {
                throw BackendFailure(code: payload.error, statusCode: httpResponse.statusCode, message: payload.message)
            }
            let fallback = String(data: data, encoding: .utf8) ?? "The backend returned HTTP \(httpResponse.statusCode)."
            throw BackendFailure(code: nil, statusCode: httpResponse.statusCode, message: fallback)
        }

        do {
            let decoder = JSONDecoder()
            return try decoder.decode(T.self, from: data)
        } catch {
            throw BackendFailure(code: nil, statusCode: httpResponse.statusCode, message: "Unable to decode backend response: \(error.localizedDescription)")
        }
    }

    private func buildRequest(path: String, query: [URLQueryItem]) throws -> URLRequest {
        guard var url = baseURL else {
            throw BackendFailure(code: "invalid_base_url", statusCode: nil, message: "The backend URL is invalid.")
        }

        let trimmedPath = path.split(separator: "/").map(String.init)
        for component in trimmedPath {
            url.append(path: component)
        }

        guard var components = URLComponents(url: url, resolvingAgainstBaseURL: false) else {
            throw BackendFailure(code: "invalid_base_url", statusCode: nil, message: "The backend URL is invalid.")
        }

        components.queryItems = query.isEmpty ? nil : query

        guard let finalURL = components.url else {
            throw BackendFailure(code: "invalid_base_url", statusCode: nil, message: "The backend URL is invalid.")
        }

        var request = URLRequest(url: finalURL)
        request.timeoutInterval = 20
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue("NVT Apple Client", forHTTPHeaderField: "User-Agent")
        return request
    }
}
