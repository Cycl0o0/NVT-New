import SwiftUI

struct NVTSectionCard<Content: View>: View {
    let title: String
    let subtitle: String?
    let tint: Color
    @ViewBuilder let content: Content

    init(title: String, subtitle: String? = nil, tint: Color, @ViewBuilder content: () -> Content) {
        self.title = title
        self.subtitle = subtitle
        self.tint = tint
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            VStack(alignment: .leading, spacing: 4) {
                Text(title)
                    .font(.title3.weight(.bold))
                    .fontDesign(.rounded)
                if let subtitle, !subtitle.isEmpty {
                    Text(subtitle)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
            }
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .nvtGlassCard()
        .overlay(alignment: .topTrailing) {
            Circle()
                .fill(tint.opacity(0.14))
                .frame(width: 120, height: 120)
                .offset(x: 28, y: -28)
        }
    }
}

struct NVTMetricTile: View {
    let label: String
    let value: String
    let symbol: String
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Image(systemName: symbol)
                .font(.headline.weight(.semibold))
                .foregroundStyle(tint)
                .padding(10)
                .background(
                    RoundedRectangle(cornerRadius: 14, style: .continuous)
                        .fill(tint.opacity(0.12))
                )

            Text(value)
                .font(.system(size: 28, weight: .heavy, design: .rounded))
                .foregroundStyle(.primary)

            Text(label.uppercased())
                .font(.caption.weight(.bold))
                .tracking(1.1)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, minHeight: 132, alignment: .leading)
        .padding(18)
        .background(
            RoundedRectangle(cornerRadius: 24, style: .continuous)
                .fill(.thinMaterial)
                .overlay(
                    RoundedRectangle(cornerRadius: 24, style: .continuous)
                        .stroke(Color.white.opacity(0.35), lineWidth: 1)
                )
        )
    }
}

struct NVTLineBadge: View {
    let line: TransitLine
    @Environment(\.colorScheme) private var colorScheme

    var backgroundColor: Color {
        if let color = line.colorBg, !color.isEmpty {
            return Color(hex: color)
        }
        return Color.accentColor
    }

    var foregroundColor: Color {
        if let color = line.colorFg, !color.isEmpty {
            return Color(hex: color)
        }
        return .white
    }
    
    // Check if the background color is too light and needs a border for visibility
    private var needsBorder: Bool {
        guard let bgHex = line.colorBg, !bgHex.isEmpty else { return false }
        let cleaned = bgHex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted).uppercased()
        var value: UInt64 = 0
        Scanner(string: cleaned).scanHexInt64(&value)
        
        let red = Double((value >> 16) & 0xFF) / 255
        let green = Double((value >> 8) & 0xFF) / 255
        let blue = Double(value & 0xFF) / 255
        
        // Calculate relative luminance
        let luminance = 0.299 * red + 0.587 * green + 0.114 * blue
        
        // In light mode, light colors need border; in dark mode, dark colors need border
        return colorScheme == .dark ? luminance < 0.25 : luminance > 0.8
    }

    var body: some View {
        Text(line.code.isEmpty ? "?" : line.code)
            .font(.headline.weight(.black))
            .fontDesign(.rounded)
            .lineLimit(1)
            .minimumScaleFactor(0.6)
            .foregroundStyle(foregroundColor)
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(
                Capsule(style: .continuous)
                    .fill(backgroundColor)
                    .overlay(
                        Capsule(style: .continuous)
                            .stroke(Color.primary.opacity(needsBorder ? 0.2 : 0), lineWidth: 1.5)
                    )
            )
    }
}

struct NVTAlertPill: View {
    let text: String
    let color: Color

    var body: some View {
        Text(text)
            .font(.caption.weight(.bold))
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(
                Capsule(style: .continuous)
                    .fill(color.opacity(0.14))
            )
            .foregroundStyle(color)
    }
}

struct NVTEmptyState: View {
    let symbol: String
    let title: String
    let message: String

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Image(systemName: symbol)
                .font(.title2.weight(.bold))
                .foregroundStyle(.secondary)
            Text(title)
                .font(.headline.weight(.bold))
                .fontDesign(.rounded)
            Text(message)
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(18)
        .background(
            RoundedRectangle(cornerRadius: 22, style: .continuous)
                .fill(.regularMaterial)
        )
    }
}

struct NVTStatusBanner: View {
    let text: String
    let color: Color

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "exclamationmark.bubble.fill")
                .foregroundStyle(color)
            Text(text)
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.primary)
            Spacer()
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .fill(color.opacity(0.12))
        )
    }
}
