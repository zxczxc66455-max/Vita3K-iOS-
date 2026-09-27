# Tsubomi (fork focus on add more BUG)

**Tsubomi** is an experimental PlayStation Vita emulator for iOS (iPhone/iPad).

Tsubomi is a **fork of [Vita3K](https://github.com/Vita3K/Vita3K)** and uses it as its
emulation base. All of the heavy lifting — the CPU, GPU (GXM→Vulkan/MoltenVK), kernel
and module HLE — is Vita3K's work; Tsubomi adds a native iOS front-end, an on-device
import pipeline, a touch controller, and the platform glue needed to run on a
non-jailbroken iPhone with JIT. Please support the upstream Vita3K project.

## ⚠️ Legal / piracy disclaimer

Tsubomi does **not** condone or support piracy or any other illegal activity.

- You must **dump your own games and firmware from hardware you own.** Do not download
  games you do not own.
- Tsubomi ships with no games, firmware, or keys, and none are provided.
- Booting Vitamin dumps or other pirated content is not supported.

By using Tsubomi you agree that you are solely responsible for the content you load and
that you are complying with the laws in your jurisdiction.

## Requirements

- An arm64 iPhone/iPad with Metal on **iOS 16.7 or later**, with a way to
  sign and sideload an unsigned `.ipa`. The compatibility target includes
  **iPhone 8 Plus on iOS 16.7.16**; game performance and device stability still
  need physical-device validation. See [iOS 16 compatibility](docs/ios-16-compatibility.md).
- **JIT** must be enabled for the default Dynarmic game backend (Tsubomi shows a
  banner and refuses JIT launches when permission is unavailable). Settings also
  offers an **experimental IR Interpreter** without JIT permission; its limited
  instruction support is not sufficient for general games. See
  [runtime settings and the 768 MiB guest budget](docs/runtime-settings.md). [StikDebug](https://github.com/StephenDev0/StikDebug)
  or another debugger/JIT enabler compatible with your iOS version is required.
  Do not assume an iOS 26 JIT tool supports iOS 16.
- Your own **PS Vita firmware** and **game dumps**.

## Setup

1. Sideload the unsigned `Tsubomi.ipa` and enable JIT for it (e.g. via StikDebug).
2. Launch Tsubomi. On first run it creates its data folder at
   **`Documents/Tsubomi`** (visible in the Files app — file sharing is enabled).
3. Add content with the **+** button in the library:
   - **Import game (.vpk / .zip / .pkg)** — a Vita app package.
   - **Import firmware (.PUP)** — a PS Vita firmware update.
   - **Import game folder (NoNpDrm)** — select an extracted game folder or a
     folder containing `app`, `patch`, and `addcont`.
   - NoNpDrm imports use the bundled **work.bin** or a matching installed license,
     and decrypt in staging before replacing the installed game. If the license
     is separate, import it first, then import the game.
     See [game import formats and troubleshooting](docs/game-import.md).
   Alternatively, copy your desktop **Vita3K** data folder into
   `Documents/Tsubomi/vita` in the Files app and tap **Refresh**.

### Data layout (`Documents/Tsubomi/`)

```
Tsubomi/
  vita/            the Vita filesystem (ux0, vs0, sa0, …)
    ux0/app/<TITLEID>/        installed games
    ux0/user/00/savedata/     save data
    ux0/license/              NoNpDrm .rif licenses
  cache/           shader / pipeline caches (persist across reinstalls)
  tsubomi.log      the log file — attach this when reporting issues
```

`Documents` survives app reinstalls, so games, saves, and firmware are kept when you
sideload a new build.

## Screenshots

![Persona 4 Golden](_readme/screenshots/Persona%204%20Golden.png)

![VA-11 HALL-A](_readme/screenshots/VA-11%20HALL-A.png)

![Amagami Ebikore+](_readme/screenshots/Amagami%20Ebikore+.png)

## Credits

- **[Vita3K](https://github.com/Vita3K/Vita3K)** and its contributors — the emulator
  Tsubomi is built on.
