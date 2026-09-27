import SwiftUI

/// The settings screen.
///
/// Deliberately plain: a `Form` in a `NavigationStack`, built from stock
/// `Toggle`/`Slider`/`Picker` rows. On iOS 26 that is what produces correct
/// Liquid Glass — the navigation bar and any sheet chrome are glass, and the
/// content underneath is opaque and scrolls beneath it. Hand-rolling glass
/// behind list rows (which the UIKit screen used to do) both fights the design
/// system and costs a refraction pass per visible row.
///
/// Everything here is a system control, so Dynamic Type, VoiceOver labels and
/// values, high-contrast, and reduce-transparency are all inherited rather than
/// reimplemented.
@MainActor
struct SettingsView: View {
    @Environment(\.scenePhase) private var scenePhase
    @ObservedObject private var library = LibraryState.shared
    @State private var deviceInformation: [String: String] = [:]
    @StateObject private var model: SettingsModel
    @ObservedObject private var runtimeLatch = RuntimeLatch.shared
    @AppStorage("tsubomi.orientationLockEnabled")
    private var orientationLockEnabled = false
    @AppStorage("tsubomi.orientationLock")
    private var orientationLock = OrientationLockOption.portrait.rawValue
    @AppStorage(NormalListArtwork.defaultsKey)
    private var normalListArtwork = NormalListArtwork.coverArt.rawValue
    @AppStorage(LibrarySortOption.defaultsKey)
    private var librarySort = LibrarySortOption.alphabetical.rawValue
    @AppStorage("tsubomi.metalArgumentBuffers") private var metalArgumentBuffers = 0
    @AppStorage("tsubomi.idleCacheSeconds") private var idleCacheSeconds = 45
    @AppStorage("tsubomi.preferHLEAvPlayer") private var preferHLEAvPlayer = false
    @State private var showingA11Confirmation = false
    @State private var profileApplied = false
    @AppStorage("tsubomi.cpuBackend") private var cpuBackend = 0
    @AppStorage("tsubomi.guestMemoryMiB") private var guestMemoryMiB = 768
    @AppStorage("tsubomi.prewarmShaderCache") private var prewarmShaderCache = true
    @AppStorage("tsubomi.precompileShaders") private var precompileShaders = false
    @AppStorage("tsubomi.jitCacheMiB") private var jitCacheMiB = 0
    @AppStorage("tsubomi.cpuExecutionThreads") private var cpuExecutionThreads = 0
    @AppStorage("tsubomi.shaderWorkers") private var shaderWorkers = 0
    @AppStorage("tsubomi.textureCacheEntries") private var textureCacheEntries = 0
    @AppStorage("tsubomi.trimStagingBuffers") private var trimStagingBuffers = true
    @AppStorage("MetalHUDForceEnabled") private var metalHUDEnabled = false
    @AppStorage("tsubomi.renderDiagnostics") private var renderDiagnostics = 0
    @AppStorage("tsubomi.conservativeCulling") private var conservativeCulling = false
    @AppStorage("tsubomi.logOpacity") private var logOpacity = 0.18
    @AppStorage("tsubomi.compactPerformanceHUD") private var compactPerformanceHUD = false
    /// Invoked when the user is done; the host controller dismisses.
    private let onFinish: () -> Void

    @State private var showingResetConfirmation = false
    @State private var showingRuntimeNotice = false

    init(scope: SettingsModel.Scope, onFinish: @escaping () -> Void) {
        _model = StateObject(wrappedValue: SettingsModel(scope: scope))
        self.onFinish = onFinish
    }

    var body: some View {
        NavigationStack {
            Form {
                if !model.isPerGame {
                    Section {
                        NavigationLink {
                            settingsPage("Performance & Memory") {
                                performanceProfileSection
                                memorySection
                                metalSection
                            }
                        } label: {
                            Label("Performance & Memory", systemImage: "speedometer")
                        }
                    } footer: {
                        Text("Tune memory use and graphics for your device. Changes apply after restarting the app.")
                    }
                }
                Section("Emulation") {
                    NavigationLink {
                        settingsPage("Graphics & Display") { videoSection; graphicsSection }
                    } label: { Label("Graphics & Display", systemImage: "cube") }
                    NavigationLink {
                        settingsPage("Audio") { audioSection }
                    } label: { Label("Audio", systemImage: "speaker.wave.2") }
                }
                if !model.isPerGame {
                    Section("Interface & Input") {
                        NavigationLink {
                            settingsPage("General") { generalSection }
                        } label: { Label("General", systemImage: "gearshape") }
                        NavigationLink {
                            settingsPage("Library & Saves") { librarySection }
                        } label: { Label("Library & Saves", systemImage: "books.vertical") }
                        NavigationLink {
                            settingsPage("Controls") { controlsSection }
                        } label: { Label("Controls", systemImage: "gamecontroller") }
                        NavigationLink {
                            settingsPage("Performance Overlay") { performanceOverlaySection }
                        } label: { Label("Performance Overlay", systemImage: "chart.xyaxis.line") }
                    }
                    Section("System") {
                        NavigationLink {
                            settingsPage("Firmware") { firmwareSection }
                        } label: { Label("Firmware", systemImage: "internaldrive") }
                    }
                    Section("Tools") {
                        NavigationLink {
                            settingsPage("Device & Runtime") { deviceSection }
                                .onAppear { deviceInformation = Bridge.deviceInformation }
                        } label: { Label("Device & Runtime", systemImage: "iphone") }
                        NavigationLink {
                            advancedPage
                        } label: { Label("Advanced Settings", systemImage: "slider.horizontal.3") }
                    }
                }
                if model.isPerGame {
                    Section("Advanced") {
                        NavigationLink("CPU & JIT") { settingsPage("CPU & JIT") { cpuSection } }
                        NavigationLink("Shaders") { settingsPage("Shaders") { shaderSection } }
                        NavigationLink("Compatibility") { settingsPage("Compatibility") { compatibilitySection } }
                    }
                    perGameResetSection
                }
            }
            .scrollContentBackground(.hidden)
            .background(InterfaceTheme.background)
            .onDisappear { model.save() }
            .onChange(of: scenePhase) { phase in
                if phase != .active { model.save() }
            }
            .navigationTitle(model.navigationTitle)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Text(model.navigationTitle)
                        .font(.headline)
                        .lineLimit(1)
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        model.save()
                        // Per-game settings persist immediately in the native
                        // frontend. Global settings play after the core reports
                        // that its commit completed.
                        if model.isPerGame {
                            HomeSoundEffects.play(.sparkle)
                        }
                        onFinish()
                    }
                }
            }
            .alert("Developer mode enabled", isPresented: $showingRuntimeNotice) {
                Button("OK", role: .cancel) {}
            }
        }
        .tint(InterfaceTheme.accent)
    }

    // MARK: - Sections

    private var generalSection: some View {
        Section {
            DefaultsToggle("Interface sound effects", key: .soundEffects)
            if #available(iOS 26.0, *) {
                DefaultsToggle("Liquid Glass", key: .liquidGlassInGame)
            }
            Toggle("Orientation lock", isOn: $orientationLockEnabled)
                .onChange(of: orientationLockEnabled) { isEnabled in
                    Bridge.setOrientationLockEnabled(isEnabled)
                }
            if orientationLockEnabled {
                Picker("Locked orientation", selection: $orientationLock) {
                    ForEach(OrientationLockOption.allCases) { option in
                        Text(option.title).tag(option.rawValue)
                    }
                }
                .onChange(of: orientationLock) { newValue in
                    Bridge.applyOrientationLock(newValue)
                }
            }
        } header: {
            Text("General")
        } footer: {
            Text("Liquid Glass is turned off in game only.")
        }
    }

    private var runtimeSection: some View {
        Section {
            Button {
                exportGames()
            } label: {
                Label("Export games", systemImage: "square.and.arrow.up")
            }
            Button {
                importGames()
            } label: {
                Label("Import games…", systemImage: "square.and.arrow.down")
            }
        } header: {
            Text("Developer")
        } footer: {
            Text("Exporting a large library takes a few minutes. A share sheet opens when the archive is ready.")
        }
    }

    /// Export runs on a background thread and reports through the library's
    /// busy overlay and status toast - both of which live *behind* this sheet.
    /// So this closes Settings first, exactly as Done does, and the work is
    /// visible instead of appearing to do nothing at all.
    private func exportGames() {
        model.save()
        onFinish()
        LibraryStateBridge.setBusy("Exporting games…")
        Bridge.exportLibraryArchive()
    }

    /// Imports need the same treatment as exportGames(): once a file is picked,
    /// the install progress and its result toast are drawn on the library,
    /// behind this sheet. Leaving Settings up made a running import look like
    /// nothing had happened until the user dragged the sheet away themselves.
    /// The picker is presented once the sheet has actually gone - presenting
    /// into a controller that is still dismissing is dropped by UIKit.
    private func importGames() {
        model.save()
        onFinish()
        Bridge.presentLibraryArchiveImportPicker()
    }

    private func settingsPage<Content: View>(_ title: String,
        @ViewBuilder content: () -> Content) -> some View {
        Form { content() }
            .scrollContentBackground(.hidden)
            .background(InterfaceTheme.background)
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .onDisappear { model.save() }
    }

    private func settingsHelp<Content: View>(@ViewBuilder content: @escaping () -> Content) -> some View {
        DisclosureGroup("Details") {
            VStack(alignment: .leading, spacing: 8, content: content)
                .padding(.top, 6)
        }
    }

    private var advancedPage: some View {
        Form {
            Section("Execution & Memory") {
                NavigationLink("CPU & JIT") { settingsPage("CPU & JIT") { cpuSection } }
                NavigationLink("Memory") { settingsPage("Memory") { memorySection } }
            }
            Section("Rendering") {
                NavigationLink("Shader compilation") { settingsPage("Shaders") { shaderSection } }
                NavigationLink("Compatibility") { settingsPage("Compatibility") { compatibilitySection } }
                NavigationLink("Diagnostics & Metal HUD") { settingsPage("Diagnostics") { diagnosticsSection } }
            }
            if runtimeLatch.revealed { runtimeSection }
        }
        .navigationTitle("Advanced Settings")
        .navigationBarTitleDisplayMode(.inline)
        .onDisappear { model.save() }
    }

    private var deviceSection: some View {
        Group {
            Section("Runtime") {
                LabeledContent("JIT status", value: library.jitAvailable ? "Available (last core check)" : "Unavailable / not checked")
                ForEach(["Active CPU backend", "Renderer", "Metal bindings", "Idle cache timeout", "AvPlayer policy", "App version"], id: \.self) { key in
                    LabeledContent(key, value: deviceInformation[key] ?? "Unknown")
                }
            }
            Section("Hardware & System") {
                ForEach(["Model", "Hardware identifier", "System", "CPU architecture", "Logical CPUs", "GPU", "Physical RAM"], id: \.self) { key in
                    LabeledContent(key, value: deviceInformation[key] ?? "Unknown")
                }
            }
            Section {
                ForEach(["App memory", "Memory headroom", "Free storage", "Thermal state", "Low Power Mode"], id: \.self) { key in
                    LabeledContent(key, value: deviceInformation[key] ?? "Unknown")
                }
                Button("Refresh snapshot") { deviceInformation = Bridge.deviceInformation }
            } header: { Text("Current Snapshot") } footer: {
                Text("Values update when this page opens or you refresh. Memory headroom is the OS estimate for this process; it is not total free RAM. JIT availability can change after the core's check.")
            }
        }
    }

    private var performanceProfileSection: some View {
        Section {
            Button("Apply A11 memory profile") { showingA11Confirmation = true }
                .confirmationDialog("Apply A11 memory profile?", isPresented: $showingA11Confirmation, titleVisibility: .visible) {
                    Button("Apply profile") {
                        guestMemoryMiB = 768
                        jitCacheMiB = 8
                        cpuExecutionThreads = 0
                        shaderWorkers = 1
                        textureCacheEntries = 128
                        trimStagingBuffers = true
                        idleCacheSeconds = 45
                        metalArgumentBuffers = 1
                        precompileShaders = false
                        prewarmShaderCache = true
                        profileApplied = true
                    }
                } message: {
                    Text("Sets an 8 MiB JIT cache per thread, one shader worker, 128 cached textures and legacy Metal bindings. Sets guest RAM to 768 MiB. Restart the app to apply.")
                }
            if profileApplied {
                Label("Profile saved. Restart the app to apply.", systemImage: "checkmark.circle")
                    .foregroundStyle(InterfaceTheme.accent)
            }
            NavigationLink("CPU & JIT") { settingsPage("CPU & JIT") { cpuSection } }
            NavigationLink("Shader compilation") { settingsPage("Shaders") { shaderSection } }
        } header: { Text("Device profile") } footer: {
            Text("A starting point for iPhone 8, 8 Plus and X. Smaller caches can cause recompilation or uploads; compare the same scene on your device.")
        }
    }

    private var metalSection: some View {
        Section {
            Picker("Metal resource bindings", selection: $metalArgumentBuffers) {
                Text("Automatic for this device").tag(0)
                Text("Legacy bindings").tag(1)
                Text("Argument buffers").tag(2)
            }
            Toggle("Use HLE video player", isOn: $preferHLEAvPlayer)
        } header: { Text("Graphics & Media") } footer: {
            settingsHelp {
                Text("Automatic uses legacy bindings on A11 and argument buffers on newer devices. Requires an app restart. Try Automatic again if legacy bindings cause missing graphics.")
                Text("HLE video player uses the existing host decoder instead of Vita AvPlayer firmware. It is optional because some games rely on firmware behavior. Other required firmware modules remain enabled. This does not enable Apple hardware video decoding.")
            }
        }
    }

    private var cpuSection: some View {
        Section {
            if !model.isPerGame {
                Picker("CPU backend", selection: $cpuBackend) {
                    Text("Dynarmic JIT").tag(0)
                    Text("IR Interpreter (experimental)").tag(1)
                }
                LabeledContent("Active backend", value: Bridge.cpuRequiresJIT ? "Dynarmic JIT" : "IR Interpreter")
                if cpuBackend == 0 {
                    Picker("Concurrent CPU threads", selection: $cpuExecutionThreads) {
                        Text("Automatic (OS scheduling)").tag(0)
                        ForEach(1...8, id: \.self) { value in
                            Text("Up to \(value)").tag(value)
                        }
                    }
                    LabeledContent("Host logical CPUs", value: "\(ProcessInfo.processInfo.activeProcessorCount)")
                    LabeledContent("JIT compiler", value: "Runs in guest CPU threads")
                    Picker("JIT cache / thread", selection: $jitCacheMiB) {
                        Text("Automatic").tag(0)
                        ForEach([4, 8, 12, 16, 24, 32], id: \.self) { value in
                            Text("\(value) MiB").tag(value)
                        }
                    }
                }
            }
            Toggle("JIT CPU optimizations", isOn: $model.cpuOptimizations)
                .disabled(cpuBackend == 1)
        } header: {
            Text("CPU")
        } footer: {
            settingsHelp {
                Text("Backend, execution thread limit and JIT cache changes take effect after closing and reopening Tsubomi. Active backend shows the backend this process is using.")
                Text("Dynarmic JIT requires JIT permission. CPU optimizations apply on the next game launch.")
                Text("IR Interpreter is experimental and slower. It supports a subset of ARM/Thumb integer instructions; VFP, NEON and exclusive instructions are not supported. Unsupported instructions stop execution and are recorded in the log.")
                Text("Automatic lets iOS schedule all runnable guest CPU threads. An explicit limit caps simultaneous JIT execution and translation, clamped to the host CPU count. It does not select physical cores, create extra game threads or change the guest affinity mask.")
                Text("Limited mode uses short instruction slices so waiting game threads can make progress. It adds scheduling overhead and may reduce FPS. Start with Automatic; compare the same scene before keeping a limit. Audio, rendering and shader workers are outside this limit.")
                Text("Dynarmic compiles missing code blocks on the guest thread that needs them. There is no separate JIT compiler pool to assign cores to. More execution threads cannot split a single game thread across cores.")
                Text("The JIT cache size is per guest thread, not a limit on total CPU memory. Automatic selects the size for this device. Raising it may reduce recompilation but increases memory use.")
            }
        }
    }

    private var memorySection: some View {
        Section {
            Picker("Guest RAM limit", selection: $guestMemoryMiB) {
                ForEach([512, 768, 1024], id: \.self) { value in
                    Text("\(value) MiB").tag(value)
                }
            }
            Picker("Texture cache", selection: $textureCacheEntries) {
                Text("Automatic").tag(0)
                ForEach([128, 256, 512], id: \.self) { value in
                    Text("\(value) textures").tag(value)
                }
            }
            Toggle("Reclaim unused GPU buffer memory", isOn: $trimStagingBuffers)
            Picker("Release idle graphics caches", selection: $idleCacheSeconds) {
                Text("Off").tag(0)
                ForEach([30, 45, 60], id: \.self) { seconds in
                    Text("After \(seconds) seconds").tag(seconds)
                }
            }
            Button("Reset memory settings") {
                guestMemoryMiB = 768
                textureCacheEntries = 0
                trimStagingBuffers = true
                idleCacheSeconds = 45
            }
        } header: {
            Text("Allocation & Caches")
        } footer: {
            settingsHelp {
                Text("Saved immediately. Close and reopen Tsubomi to apply these memory settings.")
                Text("Guest RAM limits memory allocated by the emulated game. The default is 768 MiB; games that need more may fail to allocate memory. JIT code, GPU resources and the interface use additional RAM.")
                Text("Texture cache limits count textures, not MiB. Smaller caches use fewer entries but may cause more uploads and stutter.")
                Text("Idle textures and unused descriptor pools are retired during rendering after the chosen timeout. GPU resources are freed only after their fences complete. Compiled shaders are kept to avoid compilation stutter.")
                Text("GPU buffer reclamation releases oversized upload buffers after the GPU finishes using them. Reset restores only the settings on this page.")
            }
        }
    }

    private var shaderSection: some View {
        Section {
            LabeledContent("GPU core allocation", value: "Managed by Metal / iOS")
            Toggle("Shader disk cache", isOn: $model.shaderCache)
            Toggle("Async pipeline compilation", isOn: $model.asyncPipelineCompilation)
            if !model.isPerGame {
                Picker("Shader compiler workers", selection: $shaderWorkers) {
                    Text("Automatic").tag(0)
                    ForEach(1...4, id: \.self) { value in Text("\(value)").tag(value) }
                }
                .disabled(!model.asyncPipelineCompilation)
                Toggle("Prepare cached shaders in background", isOn: $prewarmShaderCache)
                    .disabled(!model.shaderCache)
                Toggle("Precompile cached shaders at launch", isOn: $precompileShaders)
                    .disabled(!model.shaderCache)
            }
        } header: {
            Text("Shaders")
        } footer: {
            settingsHelp {
                Text("Shader disk cache reuses compiled shaders between launches. Async compilation can reduce pauses, but objects may be missing until their pipeline is ready. Turn it off when checking missing graphics.")
                Text("Shader compiler workers run on the CPU and prepare graphics pipelines. They are separate from CPU JIT execution. Metal schedules 3D work on the GPU; this renderer cannot enable a chosen number of physical GPU cores.")
                Text("Background preparation loads previously cached shader modules when games create programs. It uses a bounded queue and needs an app restart. First-time shaders and complete pipelines still need draw-time state.")
                Text("Compiler thread count and precompilation require an app restart. More threads can increase CPU and memory use; precompilation needs disk caching and can lengthen startup.")
                Text("Shader settings selected in the library apply on the next game launch.")
            }
        }
    }

    private var videoSection: some View {
        Section {
            Toggle("V-Sync", isOn: $model.vSync)
                .accessibilityHint("Synchronizes presentation to the display when supported.")
        } header: {
            Text("Video")
        } footer: {
            Text("V-Sync uses a display-synchronized presentation mode. When off, the renderer chooses an available low-latency mode; some devices still require synchronized presentation. Games keep their original timing. This does not turn a 30 FPS game into a 60 FPS game or guarantee 60 FPS.")
        }
    }

    private var graphicsSection: some View {
        Section {
            Button("Lower memory preset (0.5×)") {
                model.useLowerMemoryPreset()
            }
            // A labelled slider rather than a stepper: the multiplier is
            // continuous and the exact number matters less than the direction.
            LabeledContent("Resolution") {
                Text(model.resolutionLabel)
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }
            Slider(value: $model.resolutionMultiplier, in: 0.5...2.0, step: 0.25) {
                Text("Resolution multiplier")
            } minimumValueLabel: {
                Text("0.5×").font(.caption2)
            } maximumValueLabel: {
                Text("2×").font(.caption2)
            }
            .accessibilityValue(model.resolutionLabel)

            Picker("Anisotropic filtering", selection: $model.anisotropicFiltering) {
                ForEach(SettingsModel.anisotropicOptions, id: \.self) { value in
                    Text(SettingsModel.anisotropicLabel(value)).tag(value)
                }
            }
        } header: {
            Text("Graphics")
        } footer: {
            settingsHelp {
                Text("Resolution changes require restarting the app. Per-game overrides apply on the next launch.")
                Text("Lower resolution reduces GPU work, but games can still be limited by CPU emulation or shader compilation. The lower memory preset selects 0.5× resolution (480×272 for a native 960×544 frame).")
            }
        }
    }

    private var compatibilitySection: some View {
        Section {
            if !model.isPerGame {
                Toggle("Conservative draw culling", isOn: $conservativeCulling)
            }
            Toggle("High accuracy", isOn: $model.highAccuracy)
            Toggle("Surface sync", isOn: $model.surfaceSync)
            Toggle("Double-buffered guest memory", isOn: $model.doubleBuffer)

        } header: { Text("Compatibility") } footer: {
            settingsHelp {
                Text("Restart the app after changing these options. Compare one change at a time in the same game scene.")
                Text("Conservative draw culling skips empty draws or fully clipped draws only when the vertex program has no detected buffer access and no visibility query is active. Render-pass clears are preserved. It does not guess which game objects are hidden, remove visible HUD elements or apply distance culling. Restart the app to apply; compare the same scene with it off and on.")
                Text("High accuracy changes framebuffer feedback and surface sampling. It can improve some games and reduce performance. If it causes a black screen, turn it off for that game and report the title and log.")
                Text("Surface sync copies rendered surfaces back to guest RAM for games that read them on the CPU. It can improve effects and lighting but adds GPU readback work.")
                Text("Double-buffered guest memory copies CPU buffers for GPU use; it is not display double buffering. Leave it off if models are distorted.")
            }
        }
    }

    private var audioSection: some View {
        Section {
            Toggle("NGS audio", isOn: $model.ngsAudio)
            LabeledContent("Audio backend", value: "SDL")
        } header: {
            Text("Audio")
        } footer: {
            Text("NGS is full Vita audio emulation; disable it only while diagnosing a problem.")
        }
    }

    private var controlsSection: some View {
        Section {
            Button("Virtual controls…") {
                Bridge.presentControllerOptions()
            }
            DefaultsToggle("Colored face buttons", key: .coloredFaceButtons)
            NavigationLink("Face button layout") {
                FaceButtonLayoutView(model: model)
            }
        } header: {
            Text("Controls")
        } footer: {
            Text("Virtual controls covers opacity, scale, layout, visibility, and physical-pad auto-hide. Colored face buttons tint the on-screen ✕ ○ □ △ glyphs. Some third-party controllers report face buttons in Xbox-style positions; remap them if the wrong button responds.")
        }
    }

    private var performanceOverlaySection: some View {
        Section {
            Toggle("Compact readout", isOn: $compactPerformanceHUD)
            DefaultsToggle("Show FPS", key: .perfFPS, onEnable: enablePerfOverlay)
            DefaultsToggle("Show frametime", key: .perfFrametime, onEnable: enablePerfOverlay)
            DefaultsToggle("Show frametime graph", key: .perfFrametimeGraph, onEnable: enablePerfOverlay)
            DefaultsToggle("Show RAM usage", key: .perfRAM, onEnable: enablePerfOverlay)
            DefaultsToggle("Show battery %", key: .perfBattery, onEnable: enablePerfOverlay)
            DefaultsToggle("Show live log", key: .perfLog, onEnable: enablePerfOverlay)
            Slider(value: $logOpacity, in: 0.1...0.6, step: 0.05) {
                Text("Live log background opacity")
            }
            LabeledContent("Log background", value: "\(Int((logOpacity * 100).rounded()))%")
        } header: {
            Text("Performance overlay")
        } footer: {
            settingsHelp {
                Text("The Tsubomi overlay appears in-game once any metric is enabled. FPS counts guest frame submissions; frametime is calculated from the one-second FPS average, not measured GPU execution time. Live log shows a compact tail. Drag its header, collapse it or use × to hide it; export the log for full details.")
            }
        }
    }

    private var diagnosticsSection: some View {
        Section {
            Toggle("Apple Metal Performance HUD", isOn: $metalHUDEnabled)
            Picker("Renderer diagnostics", selection: $renderDiagnostics) {
                Text("Off").tag(0)
                Text("Summary every 5 seconds").tag(1)
                Text("Summary + shader compilation samples").tag(2)
            }
            LabeledContent("Metal HUD status", value: metalHUDEnabled ? "Requested after restart" : "Off")
            Button("Export log") { Bridge.shareLogFile() }
        } header: { Text("Diagnostics") } footer: {
            settingsHelp {
                Text("Metal HUD is requested through Apple's preference and startup environment variable. Close and reopen the app. Some iOS/provisioning combinations do not display it; the app cannot read its visibility. Tsubomi's FPS/RAM overlay remains available.")
                Text("Renderer diagnostics also require an app restart. Summaries count draw calls, flat-viewport draws, clipping, framebuffer feedback, render passes, surface copies, uploads, swapchain rebuilds and pipeline compilation. Flat draws can include 2D UI but are not an exact UI-object count.")
                Text("Shader compilation samples add at most four newly compiled shader-pair records per reporting interval; cached pipelines may produce no samples. No per-draw logging, uniform dumps, GPU waits or extra polling thread are added. Diagnostics and Metal HUD still have some overhead; keep them off for baseline FPS measurements.")
                Text("To report a 2D UI slowdown: enable Summary, reopen the app, play the same scene with the UI hidden and visible for at least 10 seconds each, then export tsubomi.log. Use sampled shaders for a short reproduction if needed.")
            }
        }
    }

    private var librarySection: some View {
        Section {
            DefaultsToggle("Show title IDs", key: .showTitleIDs, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Show version number", key: .showVersion, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Show game size", key: .showGameSize, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Wide cover art", key: .wideCoverArt)
            DefaultsToggle("Compact list", key: .compactList)
            Picker("Normal list artwork", selection: $normalListArtwork) {
                ForEach(NormalListArtwork.allCases) { option in
                    Text(option.title).tag(option.rawValue)
                }
            }
            .pickerStyle(.menu)
            Picker("Sort games by", selection: $librarySort) {
                ForEach(LibrarySortOption.allCases) { option in
                    Text(option.title).tag(option.rawValue)
                }
            }
            .pickerStyle(.menu)
            .onChange(of: librarySort) { newValue in
                LibraryState.shared.setSortOption(rawValue: newValue)
            }
            Button {
                // Same reason as exportGames(): the progress lives behind this
                // sheet, so close it first.
                model.save()
                onFinish()
                LibraryStateBridge.setBusy("Exporting saves…")
                Bridge.exportAllSaves()
            } label: {
                Label("Export all game saves", systemImage: "square.and.arrow.up")
            }
            Button {
                // Same reason as importGames(): close Settings so the import
                // progress on the library is actually visible.
                model.save()
                onFinish()
                Bridge.presentAllSaveImportPicker()
            } label: {
                Label("Import all game saves…", systemImage: "square.and.arrow.down")
            }
        } header: {
            Text("Library")
        } footer: {
            Text("Choose cover art or the square game icon for the normal list. Compact list always uses the game icon. All-save archives include save data, trophy progress, playtime, and last-played dates. Import replaces included progress; other games remain unchanged.")
        }
    }

    /// Enabling any metric un-hides an overlay the user dismissed in-game.
    private func enablePerfOverlay() {
        Bridge.performanceOverlayDidEnableMetric()
    }

    private var firmwareSection: some View {
        Section {
            LabeledContent("Firmware") {
                Text(model.firmwareVersion.isEmpty ? "Not installed" : model.firmwareVersion)
                    .foregroundStyle(.secondary)
            }
            .contentShape(Rectangle())
            .onTapGesture {
                if runtimeLatch.record(0x6D4E_13B7) {
                    showingRuntimeNotice = true
                }
            }
            if !model.firmwareReady && !model.missingFirmware.isEmpty {
                // A plain label, not an alert: this is steady-state
                // information, and the library already blocks launching.
                Label(model.missingFirmware, systemImage: "exclamationmark.triangle")
                    .foregroundStyle(.secondary)
            }
            LabeledContent("Version", value: AppInfo.versionDisplay)
                .contentShape(Rectangle())
                .onTapGesture {
                    if runtimeLatch.record(0xA29C_508D) {
                        showingRuntimeNotice = true
                    }
                }
            NavigationLink("What's New") {
                ChangelogView()
            }
            Button {
                Bridge.openBugReportForm()
            } label: {
                Text("Report a bug")
                    .foregroundStyle(.red)
            }
            Button {
                Bridge.shareLogFile()
            } label: {
                Label("Share log file", systemImage: "square.and.arrow.up")
            }
            Button("Forked from Vita3K") {
                Bridge.open(urlString: "https://github.com/Vita3K/Vita3K")
            }
            Button("Developed by @halcyonpalace") {
                Bridge.open(urlString: "https://x.com/halcyonpalace")
            }
        } header: {
            Text("About")
        }
    }

    private var perGameResetSection: some View {
        Section {
            Button("Use global settings", role: .destructive) {
                showingResetConfirmation = true
            }
            .confirmationDialog(
                "Remove this game's custom settings?",
                isPresented: $showingResetConfirmation,
                titleVisibility: .visible
            ) {
                Button("Use global settings", role: .destructive) {
                    model.resetPerGameOverrides()
                    onFinish()
                }
            } message: {
                Text("This game will follow the global settings the next time it launches.")
            }
        } footer: {
            Text("These settings apply only to this game and take effect the next time it launches.")
        }
    }
}

/// Raw values are shared with NativeFrontend.mm. The lock is off by default;
/// once enabled, Portrait is its initial choice. Both landscape directions
/// remain explicit so controls and cables can sit on the user's preferred side.
enum OrientationLockOption: String, CaseIterable, Identifiable {
    case portrait
    case landscape
    case landscapeFlipped

    var id: String { rawValue }

    var title: String {
        switch self {
        case .portrait: "Portrait"
        case .landscape: "Landscape"
        case .landscapeFlipped: "Landscape (Flipped)"
        }
    }
}

/// Face-button remapping, split out as its own screen: four related pickers is
/// more than a section should carry, and it keeps the root list short.
@MainActor
private struct FaceButtonLayoutView: View {
    @ObservedObject var model: SettingsModel

    private static let positions = ["Bottom", "Right", "Left", "Top"]

    var body: some View {
        Form {
            Section {
                picker("Cross", selection: $model.bindCross)
                picker("Circle", selection: $model.bindCircle)
                picker("Square", selection: $model.bindSquare)
                picker("Triangle", selection: $model.bindTriangle)
            } footer: {
                Text("Choose which physical button position triggers each Vita button.")
            }
        }
        .navigationTitle("Face Buttons")
        .navigationBarTitleDisplayMode(.inline)
    }

    private func picker(_ label: String, selection: Binding<Int>) -> some View {
        Picker(label, selection: selection) {
            ForEach(Array(Self.positions.enumerated()), id: \.offset) { index, name in
                Text(name).tag(index)
            }
        }
    }
}
