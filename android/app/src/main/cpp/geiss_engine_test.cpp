#include "geiss_engine.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace geiss;

int main() {
    printf("Running GeissEngine verification suite...\n");

    const struct { int w; int h; int bits; } configs[] = {
        {320, 200, 8},
        {480, 800, 8},
        {800, 480, 32},
        {480, 480, 32},
    };

    for (const auto& tc : configs) {
        GeissConfig cfg;
        cfg.colorDepthBits = tc.bits;
        cfg.verticalSizePct = 92;
        cfg.useBeatDetection = true;
        cfg.framesTilAutoSwitch = 120;

        GeissEngine engine;
        engine.Init(tc.w, tc.h, cfg);

        // Prepare synthetic PCM16 stereo audio (bass + treble)
        std::vector<int16_t> pcm(1024 * 2);
        for (int i = 0; i < 1024; ++i) {
            float s = sinf(i * 0.12f) * 18000.0f + sinf(i * 0.7f) * 7000.0f;
            pcm[i * 2] = static_cast<int16_t>(s);
            pcm[i * 2 + 1] = static_cast<int16_t>(s * 0.9f);
        }

        // Test all 25 maps, all 7 effects, all 7 waveforms + spectrum
        for (int mode = 1; mode <= NUM_MODES; ++mode) {
            engine.SelectMap(mode);
            assert(engine.GetCurrentMode() == mode);

            // Enable all effects to stress bounds
            for (int e = 0; e < NUM_EFFECTS - 1; ++e) {
                engine.SetEffect(e, true);
            }
            engine.SetWaveform(mode % (NUM_WAVES + 1));
            engine.SetSpectrumMode((mode % 4) == 0);
            engine.FeedPcm16Stereo(pcm.data(), 1024, true);

            engine.TriggerTouchBurst(0.0f, 0.0f, 1.5f);
            engine.TriggerTouchBurst(1.0f, 1.0f, 1.5f);
            engine.TriggerTouchBurst(0.5f, 0.5f, 1.0f);

            for (int f = 0; f < 4; ++f) {
                engine.RenderFrame(1.0f / 30.0f);
            }
        }

        // Test 8-bit waveform + spectrum feed & song title bitmap stamping
        uint8_t wave8[512];
        uint8_t spec8[256];
        for (int i = 0; i < 512; ++i) wave8[i] = static_cast<uint8_t>(128 + 80 * sinf(i * 0.2f));
        for (int i = 0; i < 256; ++i) spec8[i] = static_cast<uint8_t>(200 * expf(-i * 0.02f));
        engine.FeedWaveAndSpectrum8(wave8, nullptr, 512, spec8, nullptr, 256, true);

        std::vector<uint8_t> textBmp(120 * 24 * 4, 255);
        engine.SetSongTitleText("Geiss - Test Track", false);
        engine.SetOverlayTextBitmap(textBmp.data(), 120, 24, 6);
        for (int f = 0; f < 10; ++f) {
            engine.RenderFrame(1.0f / 30.0f);
        }

        // Verify non-black RGBA output & valid alpha
        const uint8_t* rgba = engine.GetRgbaBuffer();
        uint64_t lumSum = 0;
        int totalPx = tc.w * tc.h;
        for (int i = 0; i < totalPx; ++i) {
            assert(rgba[i * 4 + 3] == 255);
            lumSum += rgba[i * 4] + rgba[i * 4 + 1] + rgba[i * 4 + 2];
        }
        assert(lumSum > 0);

        // Verify preset export & load roundtrip
        PresetData p = engine.ExportCurrentPreset("TestSlot");
        assert(p.valid);
        engine.NextMap();
        engine.LoadPresetData(p);
        assert(engine.GetCurrentMode() == p.mode);
        assert(engine.GetCurrentWaveform() == p.waveform);

        // Verify resize portrait <-> landscape
        engine.Resize(tc.h, tc.w);
        assert(engine.GetWidth() == tc.h);
        assert(engine.GetHeight() == tc.w);
        engine.RenderFrame(1.0f / 30.0f);

        printf("  PASS %dx%d (%d-bit), lumSum=%llu\n", tc.w, tc.h, tc.bits, (unsigned long long)lumSum);
    }

    // Benchmark 480x800 8-bit and 32-bit
    for (int bits : {8, 32}) {
        GeissConfig cfg;
        cfg.colorDepthBits = bits;
        cfg.autoDemoWhenSilent = true;
        GeissEngine engine;
        engine.Init(480, 800, cfg);

        auto t0 = std::chrono::steady_clock::now();
        const int frames = 120;
        for (int i = 0; i < frames; ++i) {
            engine.RenderFrame(1.0f / 60.0f);
        }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / frames;
        printf("  Benchmark 480x800 %d-bit: %.2f ms/frame (%.1f FPS)\n", bits, ms, 1000.0 / ms);
    }

    printf("All GeissEngine tests passed!\n");
    return 0;
}
