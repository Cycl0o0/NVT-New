import MapKit
import SwiftUI

struct NVTMapPanel: View {
    let network: TransitNetwork
    let line: TransitLine?
    let theme: NVTNetworkTheme
    let boundary: GeometryResponse?
    let route: GeometryResponse?
    let stops: [StopGroup]
    let selectedStopKey: String?
    let vehicles: [Vehicle]

    @State private var cameraPosition: MapCameraPosition = .automatic

    private var mappableStops: [StopGroup] {
        stops.filter(\.hasCoordinate)
    }

    private var mappableVehicles: [Vehicle] {
        vehicles.filter(\.hasPosition)
    }

    var body: some View {
        NVTSectionCard(
            title: "Network Map",
            subtitle: line.map { "\($0.code) • \($0.libelle)" } ?? network.displayName,
            tint: theme.accent
        ) {
            if hasMapContent {
                Map(position: $cameraPosition) {
                    if let boundary {
                        ForEach(boundary.paths) { path in
                            MapPolyline(coordinates: path.points.map(\.coordinate))
                                .stroke(theme.secondary.opacity(0.22), lineWidth: 2)
                        }
                    }

                    if let route {
                        ForEach(route.paths) { path in
                            MapPolyline(coordinates: path.points.map(\.coordinate))
                                .stroke(theme.accent, lineWidth: path.kind == 0 ? 6 : 4)
                        }
                    }

                    ForEach(mappableStops.prefix(80)) { stop in
                        let tint = stop.key == selectedStopKey ? theme.highlight : theme.accent
                        Annotation(stop.libelle, coordinate: stop.coordinate) {
                            VStack(spacing: 4) {
                                Circle()
                                    .fill(tint)
                                    .frame(width: stop.key == selectedStopKey ? 18 : 12, height: stop.key == selectedStopKey ? 18 : 12)
                                    .overlay(Circle().stroke(Color.white, lineWidth: 2))
                                if stop.key == selectedStopKey {
                                    Text(stop.libelle)
                                        .font(.caption2.weight(.bold))
                                        .padding(.horizontal, 8)
                                        .padding(.vertical, 4)
                                        .background(.ultraThinMaterial, in: Capsule())
                                }
                            }
                        }
                    }

                    ForEach(mappableVehicles) { vehicle in
                        Annotation(vehicle.terminus, coordinate: vehicle.coordinate) {
                            Image(systemName: "location.north.line.fill")
                                .font(.caption.weight(.black))
                                .foregroundStyle(.white)
                                .padding(8)
                                .background(vehicleToneColor(vehicle.tone), in: Circle())
                                .shadow(color: .black.opacity(0.16), radius: 6, x: 0, y: 2)
                        }
                    }
                }
                .frame(minHeight: 340)
                .clipShape(RoundedRectangle(cornerRadius: 24, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: 24, style: .continuous)
                        .stroke(Color.white.opacity(0.45), lineWidth: 1)
                )
                .onAppear(perform: updateCamera)
                .onChange(of: mapSignature) {
                    updateCamera()
                }
            } else {
                NVTEmptyState(
                    symbol: "map",
                    title: "Map data is light on this network",
                    message: "Stops, passages, vehicles, and alerts still load normally, even when route geometry is unavailable."
                )
            }
        }
    }

    private var hasMapContent: Bool {
        route != nil || boundary != nil || !mappableStops.isEmpty || !mappableVehicles.isEmpty
    }

    private var mapSignature: String {
        "\(route?.paths.count ?? 0)|\(boundary?.paths.count ?? 0)|\(mappableStops.count)|\(mappableVehicles.count)|\(selectedStopKey ?? "")"
    }

    private func updateCamera() {
        if let routeBounds = route?.bounds {
            cameraPosition = .region(routeBounds.region)
            return
        }

        if let boundaryBounds = boundary?.bounds {
            cameraPosition = .region(boundaryBounds.region)
            return
        }

        let points = mappableStops.map(\.coordinate) + mappableVehicles.map(\.coordinate)
        if let bounds = GeoBounds.bounding(points: points) {
            cameraPosition = .region(bounds.region)
        }
    }
}

private extension GeoPoint {
    var coordinate: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: lat, longitude: lon)
    }
}

private extension StopGroup {
    var coordinate: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: lat ?? 0, longitude: lon ?? 0)
    }
}

private extension Vehicle {
    var coordinate: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: lat, longitude: lon)
    }
}

private extension GeoBounds {
    var region: MKCoordinateRegion {
        let center = CLLocationCoordinate2D(
            latitude: (minLat + maxLat) / 2,
            longitude: (minLon + maxLon) / 2
        )

        let span = MKCoordinateSpan(
            latitudeDelta: max((maxLat - minLat) * 1.35, 0.02),
            longitudeDelta: max((maxLon - minLon) * 1.35, 0.02)
        )

        return MKCoordinateRegion(center: center, span: span)
    }

    static func bounding(points: [CLLocationCoordinate2D]) -> GeoBounds? {
        guard let first = points.first else { return nil }
        var minLat = first.latitude
        var maxLat = first.latitude
        var minLon = first.longitude
        var maxLon = first.longitude

        for point in points.dropFirst() {
            minLat = min(minLat, point.latitude)
            maxLat = max(maxLat, point.latitude)
            minLon = min(minLon, point.longitude)
            maxLon = max(maxLon, point.longitude)
        }

        return GeoBounds(minLon: minLon, minLat: minLat, maxLon: maxLon, maxLat: maxLat)
    }
}
