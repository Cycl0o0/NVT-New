import Foundation

enum TransitNetwork: String, CaseIterable, Codable, Identifiable {
    case bordeaux
    case toulouse
    case idfm
    case sncf

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .bordeaux:
            return "Bordeaux"
        case .toulouse:
            return "Toulouse"
        case .idfm:
            return "Paris IDFM"
        case .sncf:
            return "SNCF"
        }
    }

    var shortLabel: String {
        switch self {
        case .bordeaux:
            return "BDX"
        case .toulouse:
            return "TLS"
        case .idfm:
            return "IDFM"
        case .sncf:
            return "SNCF"
        }
    }

    var queryValue: String {
        switch self {
        case .bordeaux:
            return "bdx"
        case .toulouse:
            return "tls"
        case .idfm:
            return "idfm"
        case .sncf:
            return "sncf"
        }
    }

    var requiresLineScopedStops: Bool {
        self == .idfm || self == .sncf
    }

    var supportsRoute: Bool {
        self == .bordeaux || self == .toulouse
    }

    var supportsBoundaries: Bool {
        self == .bordeaux || self == .toulouse
    }

    var symbolName: String {
        switch self {
        case .bordeaux:
            return "tram.fill"
        case .toulouse:
            return "point.topleft.down.curvedto.point.bottomright.up.fill"
        case .idfm:
            return "building.2.crop.circle"
        case .sncf:
            return "train.side.front.car"
        }
    }

    static func fromBackend(_ rawValue: String) -> TransitNetwork? {
        switch rawValue.lowercased() {
        case "bdx", "bordeaux":
            return .bordeaux
        case "tls", "toulouse":
            return .toulouse
        case "idfm", "idf", "paris", "paris-idfm":
            return .idfm
        case "sncf":
            return .sncf
        default:
            return nil
        }
    }
}

struct BackendIndex: Decodable {
    let name: String
    let version: String
    let defaultNetwork: String
    let supportedNetworks: [String]
    let endpoints: [String]

    var resolvedDefaultNetwork: TransitNetwork? {
        TransitNetwork.fromBackend(defaultNetwork)
    }

    var resolvedSupportedNetworks: [TransitNetwork] {
        supportedNetworks.compactMap(TransitNetwork.fromBackend)
    }
}

struct BackendHealth: Decodable {
    let status: String
    let backend: String
    let version: String
    let userAgent: String
    let defaultNetwork: String
    let supportedNetworks: [String]

    var resolvedDefaultNetwork: TransitNetwork? {
        TransitNetwork.fromBackend(defaultNetwork)
    }

    var resolvedSupportedNetworks: [TransitNetwork] {
        supportedNetworks.compactMap(TransitNetwork.fromBackend)
    }
}

struct LineCollectionResponse: Decodable {
    let generatedAt: String?
    let network: String
    let items: [TransitLine]
    let stats: LineStats
}

struct AlertCollectionResponse: Decodable {
    let generatedAt: String?
    let network: String
    let items: [ServiceAlert]
    let stats: AlertStats
}

struct StopCollectionResponse: Decodable {
    let generatedAt: String?
    let network: String
    let line: TransitLine?
    let items: [StopGroup]
    let total: Int
}

struct PassageCollectionResponse: Decodable {
    let generatedAt: String?
    let network: String
    let line: TransitLine?
    let items: [Passage]
    let stats: PassageStats
    let group: StopGroup?
}

struct VehicleCollectionResponse: Decodable {
    let generatedAt: String?
    let network: String
    let line: TransitLine
    let items: [Vehicle]
    let stats: VehicleStats
}

struct GeometryResponse: Decodable {
    let generatedAt: String?
    let network: String?
    let line: TransitLine?
    let bounds: GeoBounds
    let paths: [GeometryPath]
    let labels: [MapLabel]?
    let stats: RouteStats?
}

struct LineStats: Decodable {
    let total: Int
    let active: Int
    let tram: Int?
    let bus: Int?
    let other: Int?
}

struct AlertStats: Decodable {
    let total: Int
    let critical: Int
    let warning: Int
    let info: Int
    let impactedLines: Int?
}

struct PassageStats: Decodable {
    let total: Int
    let live: Int
    let delayed: Int
    let lines: Int
}

struct VehicleStats: Decodable {
    let total: Int
    let delayed: Int
    let stopped: Int
    let avgSpeed: Int?
    let aller: Int?
    let retour: Int?
    let alerts: Int?
}

struct RouteStats: Decodable {
    let total: Int
    let aller: Int?
    let retour: Int?
}

struct TransitLine: Decodable, Identifiable, Hashable {
    let gid: Int
    let ident: Int?
    let code: String
    let libelle: String
    let vehicule: String
    let active: Bool
    let sae: Bool?
    let ref: String?
    let colorBg: String?
    let colorFg: String?

    var id: Int { gid }

    var searchableText: String {
        [code, libelle, vehicule, ref ?? ""].joined(separator: " ").lowercased()
    }
}

struct ServiceAlert: Decodable, Identifiable, Hashable {
    let gid: Int?
    let id: String
    let titre: String
    let message: String
    let severite: String
    let ligneId: Int?
    let scope: String?
    let lineCode: String
    let lineName: String
    let lineType: String?
    let lineCodes: [String]?
    let colorBg: String?
    let colorFg: String?

    var stableID: String {
        if !id.isEmpty { return id }
        if let gid { return "gid-\(gid)" }
        return "\(titre)|\(message)|\(lineCode)"
    }
}

struct StopGroup: Decodable, Identifiable, Hashable {
    let key: String
    let libelle: String
    let groupe: String
    let platformCount: Int
    let gids: [Int]
    let ref: String?
    let lines: String?
    let mode: String?
    let lon: Double?
    let lat: Double?

    var id: String { key }

    var hasCoordinate: Bool {
        lon != nil && lat != nil
    }

    var searchableText: String {
        [libelle, groupe, lines ?? "", mode ?? ""].joined(separator: " ").lowercased()
    }
}

struct Passage: Decodable, Identifiable, Hashable {
    let estimated: String
    let scheduled: String
    let courseId: Int
    let lineId: Int
    let terminusGid: Int
    let terminusName: String
    let live: Bool
    let delayed: Bool
    let lineCode: String
    let lineName: String
    let lineType: String?
    let datetime: String?
    let waitingTime: String?
    let stopName: String?
    let colorBg: String?
    let colorFg: String?

    var id: String {
        [lineCode, terminusName, estimated, scheduled, String(courseId), datetime ?? ""].joined(separator: "|")
    }
}

struct Vehicle: Decodable, Identifiable, Hashable {
    let gid: Int
    let lon: Double
    let lat: Double
    let etat: String
    let retard: Int
    let delayLabel: String
    let vitesse: Int
    let vehicule: String
    let statut: String
    let sens: String
    let terminus: String
    let arret: Bool
    let arretActu: Int
    let arretSuiv: Int
    let currentStopName: String
    let nextStopName: String
    let tone: String
    let hasPosition: Bool
    let live: Bool?
    let datetime: String?
    let waitingTime: String?

    var id: String {
        gid == 0 ? "\(terminus)|\(sens)|\(currentStopName)|\(nextStopName)" : "gid-\(gid)"
    }
}

struct GeoBounds: Decodable, Hashable {
    let minLon: Double
    let minLat: Double
    let maxLon: Double
    let maxLat: Double
}

struct GeoPoint: Decodable, Hashable {
    let lon: Double
    let lat: Double
}

struct GeometryPath: Decodable, Identifiable, Hashable {
    let kind: Int
    let points: [GeoPoint]
    let id: UUID

    private enum CodingKeys: String, CodingKey {
        case kind
        case points
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        kind = try container.decode(Int.self, forKey: .kind)
        points = try container.decode([GeoPoint].self, forKey: .points)
        id = UUID()
    }
}

struct MapLabel: Decodable, Identifiable, Hashable {
    let lon: Double
    let lat: Double
    let name: String
    let rank: Int
    let id: UUID

    private enum CodingKeys: String, CodingKey {
        case lon
        case lat
        case name
        case rank
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        lon = try container.decode(Double.self, forKey: .lon)
        lat = try container.decode(Double.self, forKey: .lat)
        name = try container.decode(String.self, forKey: .name)
        rank = try container.decode(Int.self, forKey: .rank)
        id = UUID()
    }
}

struct BackendErrorPayload: Decodable {
    let error: String
    let message: String
}
