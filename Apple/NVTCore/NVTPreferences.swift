import Foundation

final class NVTPreferences {
    private enum Keys {
        static let backendBaseURL = "nvt.backendBaseURL"
        static let selectedNetwork = "nvt.selectedNetwork"
        static let selectedLineID = "nvt.selectedLineID"
        static let selectedStopKey = "nvt.selectedStopKey"
    }

    private let defaults: UserDefaults

    var backendBaseURL: String {
        didSet {
            defaults.set(normalizedBackendBaseURLString, forKey: Keys.backendBaseURL)
        }
    }

    var selectedNetwork: TransitNetwork {
        didSet {
            defaults.set(selectedNetwork.rawValue, forKey: Keys.selectedNetwork)
        }
    }

    var selectedLineID: Int? {
        didSet {
            if let selectedLineID {
                defaults.set(selectedLineID, forKey: Keys.selectedLineID)
            } else {
                defaults.removeObject(forKey: Keys.selectedLineID)
            }
        }
    }

    var selectedStopKey: String? {
        didSet {
            if let selectedStopKey, !selectedStopKey.isEmpty {
                defaults.set(selectedStopKey, forKey: Keys.selectedStopKey)
            } else {
                defaults.removeObject(forKey: Keys.selectedStopKey)
            }
        }
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        self.backendBaseURL = defaults.string(forKey: Keys.backendBaseURL) ?? "http://127.0.0.1:8080"
        self.selectedNetwork = TransitNetwork(rawValue: defaults.string(forKey: Keys.selectedNetwork) ?? "") ?? .bordeaux

        if defaults.object(forKey: Keys.selectedLineID) != nil {
            self.selectedLineID = defaults.integer(forKey: Keys.selectedLineID)
        } else {
            self.selectedLineID = nil
        }

        self.selectedStopKey = defaults.string(forKey: Keys.selectedStopKey)
    }

    var normalizedBackendBaseURLString: String {
        var value = backendBaseURL.trimmingCharacters(in: .whitespacesAndNewlines)
        if value.isEmpty {
            value = "http://127.0.0.1:8080"
        }
        if !value.contains("://") {
            value = "http://\(value)"
        }
        return value.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
    }

    var resolvedBaseURL: URL? {
        URL(string: normalizedBackendBaseURLString)
    }
}
