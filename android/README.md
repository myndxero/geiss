# Geiss Live Wallpaper for Android

A full native C++/OpenGL ES 2.0 Android Live Wallpaper and companion control app ported from **Geiss 4.30** (Screensaver & Winamp Plug-in).

## Highlights

- **Independent Universal Audio Reactivity**: Works with *any* music or media app playing on Android:
  - **Global Output Mix** (default): Reacts silently via `android.media.audiofx.Visualizer(0)` without extra foreground notifications.
  - **System Audio Capture (Android 10+)**: True stereo 16-bit PCM & C++ radix-2 FFT via `AudioPlaybackCaptureConfiguration`.
  - **Microphone**: Classic screensaver ambient sound mode.
- **Complete Feature Parity**:
  - All **25 motion maps** with star ratings (`0★–5★`) and weighted auto-switching.
  - All **8 visual effects** (`Chasers`, `Bar`, `Dots`, `Solar`, `Grid`, `Nuclide`, `Shade`, `Spectral`).
  - All **6 waveforms + Off**, **Spectrum analyzer mode**, **8-bit indexed palette & 32-bit truecolor modes**, beat-synced slide shifts, **100 preset slots (`00–99`)**, and **100 custom dissolving message slots (`00–99`)**.
  - Optional automatic **Song Title Popups** via Android `MediaSessionManager` (`NotificationListenerService`) that dissolve directly into the Geiss plasma feedback buffer.
- **Live Wallpaper Polish**:
  - OpenGL ES 2.0 GPU texture presentation (`GL_LINEAR` smooth or `GL_NEAREST` crisp retro pixels) with selectable internal render resolution (`320p`, `480p`, `720p`, `1080p`) and FPS cap (`30/45/60`).
  - Zero-drain visibility pause when screen is off or covered by apps.
  - Interactive touch plasma bursts, configurable double-tap shortcut, and home-screen swipe parallax.

## Building Locally

Requires JDK 17, Android SDK (`platforms;android-35`, `build-tools;35.0.0`), NDK `26.3.11579264`, and CMake `3.22.1`.

```bash
cd android
./gradlew testDebugUnitTest assembleDebug
```

The debug APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.

## Next Release / Roadmap

Planned items for the next release:

- [ ] **Full-screen, castable option**
  - Add an immersive full-screen visualizer mode in `MainActivity` (hiding system bars and HUD, with keep-screen-on support).
  - Add casting / external display support (e.g., Google Cast / `MediaRouter` / Android `Presentation` for secondary displays) so Geiss can be cast full-screen to a TV or external monitor.
- [ ] **120 FPS option**
  - Add a `120 FPS` choice alongside `30 / 45 / 60` in `SettingsActivity` / `GeissPreferences`.
  - Update frame pacing in `GeissWallpaperService` (currently `coerceAtLeast(8L)` ms sleep) and request high refresh rate via `Surface.setFrameRate` (Android 11+) / choreographer timing for smooth 120 Hz output.
