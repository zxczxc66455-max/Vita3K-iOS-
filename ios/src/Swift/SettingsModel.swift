import Foundation
import Combine

/// Editable state behind the settings screen.
///
/// Published state keeps settings bindings live on iOS 16 and newer.
@MainActor
final class SettingsModel: ObservableObject {
    /// What this screen is editing. Per-game hides the rows that are device
    /// properties rather than per-title preferences, and writes an override
    /// instead of committing to the global configuration.
    enum Scope: Equatable {
        case global
        /// - Parameters:
        ///   - titleID: the title being overridden.
        ///   - displayName: shown in the navigation title.
        case perGame(titleID: String, displayName: String)
    }

    let scope: Scope

    @Published var resolutionMultiplier: Float
    @Published var vSync: Bool
    @Published var fpsLimit: Int
    @Published var shaderCache: Bool
    @Published var cpuOptimizations: Bool
    @Published var ngsAudio: Bool
    @Published var asyncPipelineCompilation: Bool
    @Published var anisotropicFiltering: Int
    @Published var highAccuracy: Bool
    @Published var surfaceSync: Bool
    @Published var doubleBuffer: Bool

    @Published var bindCross: Int
    @Published var bindCircle: Int
    @Published var bindSquare: Int
    @Published var bindTriangle: Int

    let firmwareVersion: String
    let firmwareReady: Bool
    let missingFirmware: String

    private var changes: AnyCancellable?
    private var hasChanges = false
    private var autosave: AnyCancellable?
    private var overridesWereReset = false

    /// Firmware/controller-binding fields the screen shows but never edits are
    /// kept here so they can be written back untouched — the core is handed a
    /// whole settings value, so a dropped field would clear real state.
    private let original: EmulatorSettings

    init(scope: Scope) {
        self.scope = scope

        let settings: EmulatorSettings
        switch scope {
        case .global:
            settings = Bridge.currentSettings
        case .perGame(let titleID, _):
            settings = Bridge.settings(forTitle: titleID)
        }
        original = settings

        resolutionMultiplier = settings.resolutionMultiplier
        vSync = settings.vSync
        fpsLimit = settings.fpsLimit
        shaderCache = settings.shaderCache
        cpuOptimizations = settings.cpuOptimizations
        ngsAudio = settings.ngsAudio
        asyncPipelineCompilation = settings.asyncPipelineCompilation
        anisotropicFiltering = settings.anisotropicFiltering
        highAccuracy = settings.highAccuracy
        surfaceSync = settings.surfaceSync
        doubleBuffer = settings.doubleBuffer
        bindCross = settings.bindCross
        bindCircle = settings.bindCircle
        bindSquare = settings.bindSquare
        bindTriangle = settings.bindTriangle
        firmwareVersion = settings.firmwareVersion
        firmwareReady = settings.firmwareReady
        missingFirmware = settings.missingFirmware

        // Debounce sliders; the callback runs after @Published has stored values.
        changes = objectWillChange.sink { [weak self] _ in self?.hasChanges = true }
        autosave = objectWillChange
            .debounce(for: .milliseconds(300), scheduler: RunLoop.main)
            .sink { [weak self] _ in self?.save() }
    }

    var isPerGame: Bool {
        if case .perGame = scope { return true }
        return false
    }

    var navigationTitle: String {
        switch scope {
        case .global: return "Settings"
        case .perGame(_, let displayName): return displayName
        }
    }

    /// Writes the edits back through the bridge.
    func save() {
        guard hasChanges && !overridesWereReset else { return }
        hasChanges = false
        let settings = original.copy() as! EmulatorSettings
        settings.resolutionMultiplier = resolutionMultiplier
        settings.vSync = vSync
        settings.fpsLimit = fpsLimit
        settings.shaderCache = shaderCache
        settings.cpuOptimizations = cpuOptimizations
        settings.ngsAudio = ngsAudio
        settings.asyncPipelineCompilation = asyncPipelineCompilation
        settings.anisotropicFiltering = anisotropicFiltering
        settings.highAccuracy = highAccuracy
        settings.surfaceSync = surfaceSync
        settings.doubleBuffer = doubleBuffer

        switch scope {
        case .global:
            // Controller bindings describe the attached hardware, so they are
            // only editable (and only written) in the global scope.
            settings.bindCross = bindCross
            settings.bindCircle = bindCircle
            settings.bindSquare = bindSquare
            settings.bindTriangle = bindTriangle
            Bridge.apply(settings)
        case .perGame(let titleID, _):
            Bridge.apply(settings, forTitle: titleID)
        }
    }

    /// Drops this title's overrides so it follows the global settings again.
    func resetPerGameOverrides() {
        guard case .perGame(let titleID, _) = scope else { return }
        overridesWereReset = true
        autosave?.cancel()
        Bridge.resetSettings(forTitle: titleID)
    }

    /// Reduce render-target memory and shader work without changing guest timing.
    func useLowerMemoryPreset() {
        resolutionMultiplier = 0.5
        cpuOptimizations = true
        shaderCache = true
        anisotropicFiltering = 1
        doubleBuffer = false
    }

    // MARK: - Value formatting

    /// Anisotropic filtering is a power-of-two factor, presented as discrete
    /// steps rather than a free slider because only these values are valid.
    static let anisotropicOptions: [Int] = [1, 2, 4, 8, 16]

    static func anisotropicLabel(_ value: Int) -> String {
        value <= 1 ? "Off" : "\(value)×"
    }

    var resolutionLabel: String {
        String(format: "%.2f×", resolutionMultiplier)
    }
}
