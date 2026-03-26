import SwiftUI
#if canImport(UIKit)
import UIKit
#elseif canImport(AppKit)
import AppKit
#endif

// MARK: - Centralized Color Constants

enum NVTColors {
    // Semantic colors
    static let critical = Color(hex: "#B91C1C")
    static let warning = Color(hex: "#D97706")
    static let success = Color(hex: "#0F766E")
    static let info = Color(hex: "#1D4ED8")
}

struct NVTNetworkTheme {
    let accent: Color
    let secondary: Color
    let highlight: Color
    let lineGradient: LinearGradient
    let symbolName: String

    static func theme(for network: TransitNetwork) -> NVTNetworkTheme {
        switch network {
        case .bordeaux:
            return .init(
                accent: Color(hex: "#0F766E"),
                secondary: Color(hex: "#D97706"),
                highlight: Color(hex: "#F4D35E"),
                lineGradient: LinearGradient(
                    colors: [Color(hex: "#0F766E"), Color(hex: "#115E59"), Color(hex: "#F4D35E").opacity(0.65)],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                ),
                symbolName: network.symbolName
            )
        case .toulouse:
            return .init(
                accent: Color(hex: "#E76F51"),
                secondary: Color(hex: "#264653"),
                highlight: Color(hex: "#F4A261"),
                lineGradient: LinearGradient(
                    colors: [Color(hex: "#E76F51"), Color(hex: "#F4A261"), Color(hex: "#264653").opacity(0.75)],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                ),
                symbolName: network.symbolName
            )
        case .idfm:
            return .init(
                accent: Color(hex: "#1D4ED8"),
                secondary: Color(hex: "#0F172A"),
                highlight: Color(hex: "#38BDF8"),
                lineGradient: LinearGradient(
                    colors: [Color(hex: "#1D4ED8"), Color(hex: "#38BDF8"), Color(hex: "#0F172A").opacity(0.85)],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                ),
                symbolName: network.symbolName
            )
        case .sncf:
            return .init(
                accent: Color(hex: "#B91C1C"),
                secondary: Color(hex: "#111827"),
                highlight: Color(hex: "#F97316"),
                lineGradient: LinearGradient(
                    colors: [Color(hex: "#B91C1C"), Color(hex: "#F97316"), Color(hex: "#111827").opacity(0.8)],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                ),
                symbolName: network.symbolName
            )
        }
    }
}

struct NVTSceneBackground: View {
    @Environment(\.colorScheme) private var colorScheme
    
    var body: some View {
        ZStack {
            // Adaptive base gradient
            LinearGradient(
                colors: colorScheme == .dark
                    ? [Color(hex: "#1A1A2E"), Color(hex: "#16213E"), Color(hex: "#0F0F23")]
                    : [Color(hex: "#F7F1E8"), Color(hex: "#E7ECF4"), Color(hex: "#FBFBFD")],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            // Decorative circles with adaptive opacity
            Circle()
                .fill(Color(hex: "#F59E0B").opacity(colorScheme == .dark ? 0.08 : 0.14))
                .frame(width: 520, height: 520)
                .offset(x: -260, y: -280)
                .blur(radius: 10)

            Circle()
                .fill(Color(hex: "#0891B2").opacity(colorScheme == .dark ? 0.08 : 0.14))
                .frame(width: 560, height: 560)
                .offset(x: 260, y: -170)
                .blur(radius: 20)

            Circle()
                .fill(Color(hex: "#0F172A").opacity(colorScheme == .dark ? 0.15 : 0.08))
                .frame(width: 420, height: 420)
                .offset(x: 180, y: 340)
                .blur(radius: 24)
        }
    }
}

extension Color {
    init(hex: String) {
        let cleaned = hex
            .trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
            .uppercased()
        var value: UInt64 = 0
        Scanner(string: cleaned).scanHexInt64(&value)

        let red, green, blue, alpha: UInt64
        switch cleaned.count {
        case 6:
            (red, green, blue, alpha) = ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 0xFF)
        case 8:
            (red, green, blue, alpha) = ((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)
        default:
            (red, green, blue, alpha) = (0x33, 0x33, 0x33, 0xFF)
        }

        self.init(
            .sRGB,
            red: Double(red) / 255,
            green: Double(green) / 255,
            blue: Double(blue) / 255,
            opacity: Double(alpha) / 255
        )
    }
}

extension View {
    func nvtGlassCard(padding: CGFloat = 18) -> some View {
        self
            .padding(padding)
            .background(
                RoundedRectangle(cornerRadius: 28, style: .continuous)
                    .fill(.regularMaterial)
                    .overlay(
                        RoundedRectangle(cornerRadius: 28, style: .continuous)
                            .stroke(Color.primary.opacity(0.1), lineWidth: 1)
                    )
                    .shadow(color: Color.black.opacity(0.08), radius: 24, x: 0, y: 10)
            )
    }
}
