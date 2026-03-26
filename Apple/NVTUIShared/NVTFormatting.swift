import SwiftUI

func severityColor(for severity: String) -> Color {
    let normalized = severity.lowercased()
    if normalized.contains("3_") || normalized.contains("critical") {
        return NVTColors.critical
    }
    if normalized.contains("2_") || normalized.contains("warning") || normalized.contains("retard") {
        return NVTColors.warning
    }
    return NVTColors.success
}

func vehicleToneColor(_ tone: String) -> Color {
    switch tone.lowercased() {
    case "critical":
        return NVTColors.critical
    case "warning":
        return NVTColors.warning
    default:
        return NVTColors.success
    }
}

func relativeTimestamp(_ date: Date?) -> String {
    guard let date else { return "Not refreshed yet" }
    return date.formatted(.relative(presentation: .named))
}

func coordinateSummary(for stop: StopGroup) -> String {
    if stop.hasCoordinate {
        return stop.groupe
    }
    return "\(stop.groupe) • no map point"
}
