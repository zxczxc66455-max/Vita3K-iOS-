import SwiftUI
import UIKit

/// Opaque surfaces avoid live backdrop work over the emulator's Metal view.
/// Dynamic colors follow appearance and Increase Contrast without stored state.
enum InterfaceTheme {
    static let background = Color(uiColor: backgroundColor)
    static let surface = Color(uiColor: .secondarySystemGroupedBackground)
    static let accent = Color(uiColor: UIColor { traits in
        if traits.userInterfaceStyle == .dark {
            return UIColor(red: 0.48, green: 0.79, blue: 0.75, alpha: 1)
        }
        return traits.accessibilityContrast == .high
            ? UIColor(red: 0.10, green: 0.32, blue: 0.30, alpha: 1)
            : UIColor(red: 0.16, green: 0.40, blue: 0.38, alpha: 1)
    })
    static let backgroundColor = UIColor { traits in
        if traits.accessibilityContrast == .high {
            return traits.userInterfaceStyle == .dark ? .black : .systemGroupedBackground
        }
        return traits.userInterfaceStyle == .dark
            ? UIColor(red: 0.07, green: 0.09, blue: 0.10, alpha: 1)
            : UIColor(red: 0.94, green: 0.96, blue: 0.95, alpha: 1)
    }
}
