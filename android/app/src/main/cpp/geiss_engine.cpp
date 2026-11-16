// Geiss Android Live Wallpaper - Portable C++ Visualization Engine
// Derived from Geiss Screensaver & Winamp Plug-in (3-Clause BSD License)
// Original Copyright (c) 1998-2022 Ryan Geiss (@geissomatik)

#include "geiss_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace geiss {

namespace {
constexpr float kPi = 3.14159265358979323846f;

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Lightweight in-place Cooley-Tukey radix-2 FFT for PCM16 spectrum mode
void ComputeFftMagnitudes(const float* samples, int n, uint8_t* outSpec, int outLen) {
    // n must be power of 2 (512)
    std::vector<float> re(n, 0.0f);
    std::vector<float> im(n, 0.0f);
    for (int i = 0; i < n; ++i) {
        // Hann window
        float w = 0.5f * (1.0f - cosf(2.0f * kPi * i / (float)(n - 1)));
        re[i] = samples[i] * w;
    }

    // Bit-reversal permutation
    int j = 0;
    for (int i = 0; i < n; ++i) {
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
    }

    // Butterfly stages
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * kPi / (float)len;
        float wlenCos = cosf(ang);
        float wlenSin = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float wCos = 1.0f;
            float wSin = 0.0f;
            int half = len >> 1;
            for (int k = 0; k < half; ++k) {
                int idx1 = i + k;
                int idx2 = idx1 + half;
                float uRe = re[idx1];
                float uIm = im[idx1];
                float vRe = re[idx2] * wCos - im[idx2] * wSin;
                float vIm = re[idx2] * wSin + im[idx2] * wCos;
                re[idx1] = uRe + vRe;
                im[idx1] = uIm + vIm;
                re[idx2] = uRe - vRe;
                im[idx2] = uIm - vIm;
                float nextCos = wCos * wlenCos - wSin * wlenSin;
                wSin = wCos * wlenSin + wSin * wlenCos;
                wCos = nextCos;
            }
        }
    }

    int bins = std::min(outLen, n / 2);
    for (int i = 0; i < bins; ++i) {
        float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) / (float)(n / 4);
        // Log-like compression to match Winamp 0..255 spectrumData
        float scaled = log1pf(mag * 4.0f) * 68.0f;
        int v = (int)std::clamp(scaled, 0.0f, 255.0f);
        outSpec[i] = static_cast<uint8_t>(v);
    }
    for (int i = bins; i < outLen; ++i) {
        outSpec[i] = 0;
    }
}

const char* kModeNames[NUM_MODES + 1] = {
    "Unknown",
    "Classic Spiral",
    "Outward Vortex",
    "Terra-Landing",
    "Sphere",
    "Super-Perspective",
    "Gravity Vortices",
    "Fuzzy Zoom",
    "Ripples",
    "Flower Feedback",
    "Perspective Horizon",
    "Dual Rotation Dither",
    "Sideways Splitter",
    "Continuous Black Hole",
    "Split-World Warp",
    "Flower Petals",
    "Crystal Ball",
    "Horizontal Tunnel",
    "Vertical Tunnel",
    "Tunnel Vortex",
    "Terra Horizon",
    "Diced Cube",
    "Phonic Rings",
    "Phonic Rings (Fast)",
    "Fast Swirl",
    "Hyper Zoom (1/r)"
};

} // namespace

const char* GeissEngine::GetModeName(int modeId) {
    if (modeId < 1 || modeId > NUM_MODES) return kModeNames[0];
    return kModeNames[modeId];
}

GeissEngine::GeissEngine() {
    auto seed = static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    rngState_ = seed ? seed : 0x13579BDFu;

    for (int i = 0; i <= NUM_MODES; ++i) {
        modeprefs_[i] = 3;
    }
    modeprefs_[0] = 0;
    modeprefs_total_ = NUM_MODES * 3;

    // Initialize mode flags matching main.cpp
    for (int i = 0; i <= NUM_MODES; ++i) {
        mode_motion_dampened_[i] = (i <= 16);
        rotation_dither_[i] = false;
        custom_motion_vectors_[i] = false;
    }
    mode_motion_dampened_[3] = false;
    mode_motion_dampened_[6] = false;
    rotation_dither_[1] = true;
    rotation_dither_[9] = true;
    rotation_dither_[11] = true;
    custom_motion_vectors_[6] = true;
    custom_motion_vectors_[10] = true;
    custom_motion_vectors_[12] = true;

    inWaveL_.assign(1024, 128);
    inWaveR_.assign(1024, 128);
    inSpecL_.assign(512, 0);
    inSpecR_.assign(512, 0);
}

GeissEngine::~GeissEngine() = default;

int GeissEngine::RandInt(int maxExclusive) {
    if (maxExclusive <= 1) return 0;
    uint32_t x = rngState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState_ = x;
    return static_cast<int>(x % static_cast<uint32_t>(maxExclusive));
}

float GeissEngine::RandFloat() {
    return static_cast<float>(RandInt(10000)) * 0.0001f;
}

void GeissEngine::Init(int width, int height, const GeissConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex_);

    config_ = cfg;
    if (config_.colorDepthBits != 8 && config_.colorDepthBits != 32) {
        config_.colorDepthBits = 8;
    }
    volpos_ = std::clamp(config_.volpos, 0, 20);
    volscale_ = powf(1.25f, static_cast<float>(volpos_ - 10));

    FXW_ = std::max(160, width);
    FXH_ = std::max(160, height);
    chaser_offset_ = RandInt(40000);

    for (int z = 0; z < 256; ++z) {
        remap32_[z] = static_cast<uint8_t>(std::min(255, z * 2));
    }

    for (int z = 0; z < 20; ++z) {
        chaser_x_[z] = FXW_ / 2;
        chaser_y_[z] = FXH_ / 2;
    }

    for (int x = 0; x <= 20; ++x) {
        for (int z = 0; z <= 20; ++z) {
            sqrt_tab_[x][z] = sqrtf(static_cast<float>((x - 10) * (x - 10) + (z - 10) * (z - 10)));
        }
    }

    for (int z = 0; z < 10; ++z) {
        micro_c1_[z] = 0.08f + 0.09f * (RandInt(1000) * 0.001f);
        micro_c2_[z] = 0.08f + 0.09f * (RandInt(1000) * 0.001f);
        micro_c3_[z] = 0.08f + 0.09f * (RandInt(1000) * 0.001f);
        micro_f1_[z] = 0.10f + 0.05f * (RandInt(1000) * 0.001f);
        micro_f2_[z] = 0.10f + 0.05f * (RandInt(1000) * 0.001f);
        micro_f3_[z] = 0.10f + 0.05f * (RandInt(1000) * 0.001f);
        micro_f4_[z] = 0.10f + 0.05f * (RandInt(1000) * 0.001f);
        micro_rad_[0][z] = 2.0f + 2.8f * (RandInt(1000) * 0.001f);
        micro_rad_[1][z] = 2.0f + 2.8f * (RandInt(1000) * 0.001f);
        micro_rad_[2][z] = 2.0f + 2.8f * (RandInt(1000) * 0.001f);
        micro_rad_[3][z] = 2.0f + 2.8f * (RandInt(1000) * 0.001f);
    }

    for (int z = 0; z < 6; ++z) {
        gF_[z] = (RandInt(1000) * 0.001f) * 0.01f + 0.02f;
    }
    for (int z = 0; z < 2345; ++z) {
        rand_array_[z] = (RandInt(100)) * 0.0005f;
    }

    InitModeInfo();
    AllocateBuffers();

    // Initial palette (also needed before first 8-bit frame)
    RandomPalette(false);
    for (int i = 0; i < 256; ++i) {
        ape_[i] = ape2_[i];
        apetemp_[i] = ape2_[i];
    }
    iBlendsLeftInPal_ = 0;

    PickRandomMode();
    g_rush_map_ = true;
    y_map_pos_ = -1;
    do {
        GenerateChunkOfNewMap(false, nullptr);
    } while (y_map_pos_ != -1);

    intframe_ = 0;
    floatframe_ = 0.0f;
}

void GeissEngine::AllocateBuffers() {
    int vertPct = std::clamp(config_.verticalSizePct, 30, 100);
    FX_YCUT_ = std::max(4, (FXH_ * (100 - vertPct)) / 200);
    if (FX_YCUT_ * 2 >= FXH_ - 20) {
        FX_YCUT_ = std::max(4, (FXH_ - 20) / 2);
    }
    FX_YCUT_HIDE_ = FX_YCUT_ + 2;
    FX_YCUT_NUM_LINES_ = FXW_ * (FXH_ - FX_YCUT_ * 2);
    FXW_x_FXH_ = FXW_ * FXH_;
    int minBufSize = (314 + WAVE_5_BLEND_RANGE) * 2 + 20;
    BUFSIZE_ = std::max(std::max(FXW_ * 2, FXH_ * 2), minBufSize);

    size_t pxBytes = (config_.colorDepthBits == 8) ? 1u : 4u;
    vs1_.assign(static_cast<size_t>(FXW_x_FXH_) * pxBytes, 0);
    vs2_.assign(static_cast<size_t>(FXW_x_FXH_) * pxBytes, 0);
    mapCurr_.resize(static_cast<size_t>(FXW_x_FXH_));
    mapNext_.resize(static_cast<size_t>(FXW_x_FXH_));
    rgbaOut_.assign(static_cast<size_t>(FXW_x_FXH_) * 4u, 0);

    // Ensure alpha channel is 255 in rgbaOut_
    for (int i = 0; i < FXW_x_FXH_; ++i) {
        rgbaOut_[i * 4 + 3] = 255;
    }

    soundBuffer_.assign(static_cast<size_t>(BUFSIZE_ + 64), 0);
    fSoundBuffer_.assign(static_cast<size_t>(BUFSIZE_ + 64), 0.0f);

    gXC_ = FXW_ / 2;
    gYC_ = FXH_ / 2;
    new_gXC_ = gXC_;
    new_gYC_ = gYC_;
}

void GeissEngine::Resize(int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    int w = std::max(160, width);
    int h = std::max(160, height);
    if (w == FXW_ && h == FXH_) return;

    FXW_ = w;
    FXH_ = h;
    AllocateBuffers();

    // Regenerate current mode cleanly for new aspect/size
    new_mode_ = mode_;
    g_rush_map_ = true;
    y_map_pos_ = -1;
    do {
        GenerateChunkOfNewMap(false, nullptr);
    } while (y_map_pos_ != -1);
}

void GeissEngine::UpdateConfig(const GeissConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex_);

    bool needRealloc = (cfg.colorDepthBits != config_.colorDepthBits) ||
                       (cfg.verticalSizePct != config_.verticalSizePct);
    bool palChanged = (cfg.gamma != config_.gamma) ||
                      (cfg.solarPalFreq != config_.solarPalFreq) ||
                      (cfg.coarsePalFreq != config_.coarsePalFreq);

    config_ = cfg;
    if (config_.colorDepthBits != 8 && config_.colorDepthBits != 32) {
        config_.colorDepthBits = 8;
    }
    volpos_ = std::clamp(config_.volpos, 0, 20);
    volscale_ = powf(1.25f, static_cast<float>(volpos_ - 10));

    if (needRealloc) {
        InitModeInfo();
        AllocateBuffers();
        new_mode_ = mode_;
        g_rush_map_ = true;
        y_map_pos_ = -1;
        do {
            GenerateChunkOfNewMap(false, nullptr);
        } while (y_map_pos_ != -1);
    }
    if (palChanged && config_.colorDepthBits == 8 && !bPalLocked_) {
        RandomPalette(true);
    }
}

void GeissEngine::InitModeInfo() {
    for (int z = 1; z <= NUM_MODES; ++z) {
        modeInfo_[z] = ModeInfo{};
        modeInfo_[z].min_effects = 1;
        modeInfo_[z].max_effects = 3;
        modeInfo_[z].center_dwindle = 1.0f;
        modeInfo_[z].solar_max = 60;
    }

    modeInfo_[1].effect_freq[EFF_CHASERS] = 220;
    modeInfo_[1].effect_freq[EFF_BAR]     = 150;
    modeInfo_[1].effect_freq[EFF_DOTS]    = 10;
    modeInfo_[1].effect_freq[EFF_SOLAR]   = 680;
    modeInfo_[1].effect_freq[EFF_GRID]    = 4;
    modeInfo_[1].effect_freq[EFF_NUCLIDE] = 170;
    modeInfo_[1].effect_freq[EFF_SHADE]   = 400;
    modeInfo_[1].solar_max = (config_.colorDepthBits == 8) ? 400 : 800;
    modeInfo_[1].center_dwindle = 1.0f;

    modeInfo_[2].effect_freq[EFF_CHASERS] = 750;
    modeInfo_[2].effect_freq[EFF_BAR]     = 500;
    modeInfo_[2].effect_freq[EFF_DOTS]    = 750;
    modeInfo_[2].effect_freq[EFF_SOLAR]   = 750;
    modeInfo_[2].solar_max = 35;
    modeInfo_[2].max_effects = 5;

    modeInfo_[3].effect_freq[EFF_CHASERS] = 100;
    modeInfo_[3].effect_freq[EFF_BAR]     = 100;
    modeInfo_[3].effect_freq[EFF_DOTS]    = 100;
    modeInfo_[3].effect_freq[EFF_SOLAR]   = 500;
    modeInfo_[3].effect_freq[EFF_GRID]    = 10;
    modeInfo_[3].effect_freq[EFF_SHADE]   = 300;
    modeInfo_[3].solar_max = 60;
    modeInfo_[3].center_dwindle = 0.99f;

    modeInfo_[4].effect_freq[EFF_CHASERS] = 500;
    modeInfo_[4].effect_freq[EFF_BAR]     = 100;
    modeInfo_[4].effect_freq[EFF_DOTS]    = 100;
    modeInfo_[4].effect_freq[EFF_SOLAR]   = 100;
    modeInfo_[4].effect_freq[EFF_GRID]    = 30;
    modeInfo_[4].solar_max = 34;
    modeInfo_[4].center_dwindle = 0.98f;

    modeInfo_[5].effect_freq[EFF_CHASERS] = 100;
    modeInfo_[5].effect_freq[EFF_BAR]     = 350;
    modeInfo_[5].effect_freq[EFF_DOTS]    = 100;
    modeInfo_[5].effect_freq[EFF_SOLAR]   = 500;
    modeInfo_[5].effect_freq[EFF_GRID]    = 15;
    modeInfo_[5].effect_freq[EFF_NUCLIDE] = 180;
    modeInfo_[5].effect_freq[EFF_SHADE]   = 500;
    modeInfo_[5].solar_max = 60;
    modeInfo_[5].center_dwindle = 0.99f;

    modeInfo_[6].effect_freq[EFF_CHASERS] = 400;
    modeInfo_[6].effect_freq[EFF_BAR]     = 120;
    modeInfo_[6].effect_freq[EFF_DOTS]    = 200;
    modeInfo_[6].solar_max = 60;

    modeInfo_[7].effect_freq[EFF_CHASERS] = 50;
    modeInfo_[7].effect_freq[EFF_BAR]     = 200;
    modeInfo_[7].effect_freq[EFF_SOLAR]   = 300;
    modeInfo_[7].effect_freq[EFF_NUCLIDE] = 600;
    modeInfo_[7].effect_freq[EFF_SHADE]   = 350;
    modeInfo_[7].solar_max = 65;
    modeInfo_[7].center_dwindle = 0.985f;

    modeInfo_[8].effect_freq[EFF_CHASERS] = 150;
    modeInfo_[8].effect_freq[EFF_BAR]     = 150;
    modeInfo_[8].effect_freq[EFF_DOTS]    = 150;
    modeInfo_[8].effect_freq[EFF_SOLAR]   = 150;
    modeInfo_[8].effect_freq[EFF_GRID]    = 25;
    modeInfo_[8].solar_max = 60;
    modeInfo_[8].center_dwindle = 0.96f;

    modeInfo_[9].effect_freq[EFF_CHASERS] = 450;
    modeInfo_[9].effect_freq[EFF_BAR]     = 200;
    modeInfo_[9].effect_freq[EFF_DOTS]    = 50;
    modeInfo_[9].effect_freq[EFF_SOLAR]   = 200;
    modeInfo_[9].effect_freq[EFF_NUCLIDE] = 100;
    modeInfo_[9].effect_freq[EFF_SHADE]   = 200;
    modeInfo_[9].solar_max = 50;
    modeInfo_[9].center_dwindle = 0.985f;

    modeInfo_[10].effect_freq[EFF_CHASERS] = 150;
    modeInfo_[10].effect_freq[EFF_BAR]     = 20;
    modeInfo_[10].effect_freq[EFF_DOTS]    = 80;
    modeInfo_[10].effect_freq[EFF_NUCLIDE] = 80;
    modeInfo_[10].solar_max = 0;
    modeInfo_[10].center_dwindle = 1.0f;
    modeInfo_[10].min_effects = 0;
    modeInfo_[10].max_effects = 2;

    modeInfo_[11].effect_freq[EFF_CHASERS] = 360;
    modeInfo_[11].effect_freq[EFF_BAR]     = 200;
    modeInfo_[11].effect_freq[EFF_DOTS]    = 230;
    modeInfo_[11].effect_freq[EFF_SOLAR]   = 550;
    modeInfo_[11].effect_freq[EFF_GRID]    = 10;
    modeInfo_[11].effect_freq[EFF_NUCLIDE] = 330;
    modeInfo_[11].effect_freq[EFF_SHADE]   = 150;
    modeInfo_[11].solar_max = 750;
    modeInfo_[11].min_effects = 0;
    modeInfo_[11].max_effects = 4;

    modeInfo_[12].effect_freq[EFF_CHASERS] = 360;
    modeInfo_[12].effect_freq[EFF_BAR]     = 200;
    modeInfo_[12].effect_freq[EFF_DOTS]    = 230;
    modeInfo_[12].effect_freq[EFF_NUCLIDE] = 330;
    modeInfo_[12].solar_max = 500;
    modeInfo_[12].center_dwindle = 0.915f;
    modeInfo_[12].min_effects = 0;
    modeInfo_[12].max_effects = 2;

    modeInfo_[13].effect_freq[EFF_CHASERS] = 500;
    modeInfo_[13].effect_freq[EFF_DOTS]    = 100;
    modeInfo_[13].effect_freq[EFF_GRID]    = 30;
    modeInfo_[13].solar_max = 34;
    modeInfo_[13].center_dwindle = 0.98f;

    modeInfo_[14].effect_freq[EFF_CHASERS] = 500;
    modeInfo_[14].effect_freq[EFF_DOTS]    = 100;
    modeInfo_[14].effect_freq[EFF_GRID]    = 30;
    modeInfo_[14].solar_max = 34;
    modeInfo_[14].center_dwindle = 0.98f;

    modeInfo_[15].effect_freq[EFF_NUCLIDE] = 200;
    modeInfo_[15].center_dwindle = 1.0f;
    modeInfo_[15].min_effects = 0;
    modeInfo_[15].max_effects = 1;

    modeInfo_[16].effect_freq[EFF_CHASERS] = 500;
    modeInfo_[16].effect_freq[EFF_BAR]     = 100;
    modeInfo_[16].effect_freq[EFF_DOTS]    = 100;
    modeInfo_[16].effect_freq[EFF_SOLAR]   = 100;
    modeInfo_[16].effect_freq[EFF_GRID]    = 30;
    modeInfo_[16].solar_max = 34;
    modeInfo_[16].center_dwindle = 0.98f;

    for (int z = 17; z <= NUM_MODES; ++z) {
        modeInfo_[z].effect_freq[EFF_CHASERS] = 150;
        modeInfo_[z].effect_freq[EFF_BAR]     = 150;
        modeInfo_[z].effect_freq[EFF_DOTS]    = 150;
        modeInfo_[z].effect_freq[EFF_SOLAR]   = 150;
        modeInfo_[z].effect_freq[EFF_GRID]    = 12;
        modeInfo_[z].effect_freq[EFF_SHADE]   = 50;
        modeInfo_[z].solar_max = 600;
        modeInfo_[z].min_effects = 1;
        modeInfo_[z].max_effects = 3;
        modeInfo_[z].center_dwindle = 1.0f;
    }

    for (int z = 1; z <= NUM_MODES; ++z) {
        if (config_.colorDepthBits > 8) {
            modeInfo_[z].effect_freq[EFF_NUCLIDE] = std::clamp((int)(modeInfo_[z].effect_freq[EFF_NUCLIDE] * 1.3f), 0, 900);
            modeInfo_[z].effect_freq[EFF_CHASERS] = std::clamp(modeInfo_[z].effect_freq[EFF_CHASERS] - 50, 0, 900);
            modeInfo_[z].effect_freq[EFF_DOTS]    = std::clamp(modeInfo_[z].effect_freq[EFF_DOTS] + 220, 0, 900);
            modeInfo_[z].effect_freq[EFF_BAR]     = std::clamp(modeInfo_[z].effect_freq[EFF_BAR] + 220, 0, 900);
            modeInfo_[z].effect_freq[EFF_SHADE]   = std::clamp(modeInfo_[z].effect_freq[EFF_SHADE] + 150, 0, 900);
        }
        modeInfo_[z].effect_freq[EFF_GRID] = std::min(1000, modeInfo_[z].effect_freq[EFF_GRID] + 8);
    }

    modeInfo_[20].center_dwindle = 0.98f;
    modeInfo_[21].center_dwindle = 0.98f;
    modeInfo_[22].center_dwindle = 0.98f;
    modeInfo_[23].center_dwindle = 0.98f;
}

void GeissEngine::PickRandomMode() {
    if (modeprefs_total_ <= 0) {
        new_mode_ = 1 + RandInt(NUM_MODES);
        if (RandInt(25) == 0) new_mode_ = 7;
        if (RandInt(25) == 0) new_mode_ = 5;
        y_map_pos_ = -1;
    } else {
        int a = RandInt(modeprefs_total_);
        int b = 0;
        for (int i = 1; i <= NUM_MODES; ++i) {
            b += modeprefs_[i];
            if (a < b) {
                new_mode_ = i;
                y_map_pos_ = -1;
                break;
            }
        }
    }
}

void GeissEngine::GenerateChunkOfNewMap(bool loadPreset, const PresetData* preset) {
    if (y_map_pos_ == -1) {
        y_map_pos_ = FX_YCUT_ * FXW_;
    }

    int minPos = FX_YCUT_ * FXW_;
    int maxPos = (FXH_ - FX_YCUT_) * FXW_;
    if (y_map_pos_ < minPos) y_map_pos_ = minPos;
    if (y_map_pos_ > maxPos) y_map_pos_ = maxPos;

    // Aspect-normalized coordinates so portrait phones behave identically to landscape
    float refSpan = (FXW_ >= FXH_) ? static_cast<float>(FXW_) : static_cast<float>(FXH_) * 0.85f;
    float inv_FXW = 2.0f / std::max(1.0f, refSpan);
    float half_FXW = 0.5f * refSpan;
    float rmult = 640.0f / static_cast<float>(std::max(FXW_, FXH_));
    float protective_factor = (std::max(FXW_, FXH_) > 640) ? 640.0f / static_cast<float>(std::max(FXW_, FXH_)) : 1.0f;

    if (y_map_pos_ == minPos) {
        g_bLoadPreset_ = loadPreset;
        if (loadPreset && preset) {
            pendingPreset_ = *preset;
            new_mode_ = std::clamp(preset->mode, 1, NUM_MODES);
            new_gXC_ = std::clamp((int)(preset->x_center * FXW_), 20, FXW_ - 20);
            new_gYC_ = std::clamp((int)(preset->y_center * FXH_), FX_YCUT_ + 10, FXH_ - FX_YCUT_ - 10);
            weightsum_ = std::clamp(preset->magic, 200, 256);
            new_damping_ = std::clamp(preset->damping, 0.25f, 1.0f);
            new_waveform_ = std::clamp(preset->waveform, 0, NUM_WAVES);
            bSlideShift_ = preset->shift;
            config_.gamma = std::clamp(preset->gamma, 0, 100);
            SetVolumePos(preset->volpos);
            visMode_ = preset->spectrum ? VIS_SPECTRUM : VIS_WAVE;
        } else {
            int cxJitter = std::max(10, FXW_ / 12);
            int cyJitter = std::max(10, FXH_ / 16);
            new_gXC_ = FXW_ / 2 + RandInt(cxJitter * 2 + 1) - cxJitter;
            new_gYC_ = FXH_ / 2 + RandInt(cyJitter * 2 + 1) - cyJitter;
            weightsum_ = 256;
            new_damping_ = std::clamp(suggested_damping_, 0.50f, 1.00f);
            if (mode_motion_dampened_[new_mode_]) new_damping_ *= 0.5f;

            do {
                new_waveform_ = (RandInt(NUM_WAVES * 3 - 1)) / 3 + 1;
            } while ((new_mode_ == 6 && new_waveform_ == 5) ||
                     (new_mode_ == 12 && (new_waveform_ == 4 || new_waveform_ == 6)) ||
                     (new_mode_ == 14 && (new_waveform_ == 3 || new_waveform_ == 4)) ||
                     ((new_mode_ == 8 || new_mode_ == 23 || new_mode_ == 24) && new_waveform_ == 6));
        }

        fBigBeatThreshold_ = 1.10f;
        frames_this_mode_ = 0;
        frames_crunching_this_mode_ = 0;

        if (new_mode_ == 1) {
            float r01 = RandInt(1000) * 0.001f;
            scale1_ = 0.985f - 0.12f * r01 * r01;
            scale2_ = scale1_;
            turn1_ = 0.01f + 0.01f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
            if (scale1_ > 0.97f && RandInt(3) == 1) turn1_ *= -1.0f;
        } else if (new_mode_ == 2) {
            scale1_ = 1.00f + 0.02f * (RandInt(1000) * 0.001f);
            turn1_ = 0.02f + 0.07f * (RandInt(1000) * 0.001f);
        } else if (new_mode_ == 3) {
            scale1_ = 0.85f + 0.10f * (RandInt(1000) * 0.001f);
            scale2_ = scale1_;
            turn1_ = 0.01f + 0.015f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        } else if (new_mode_ == 4) {
            turn1_ = 0.007f + 0.02f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        } else if (new_mode_ == 5) {
            turn1_ = 0.01f + 0.03f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        } else if (new_mode_ == 6) {
            int numPushTypes = 3;
            int ySpan = std::max(10, FXH_ - FX_YCUT_ * 2);
            for (int x = 0; x < 10; ++x) {
                cx_[x] = (RandInt(FXW_ * 10)) * 0.1f;
                cy_[x] = FX_YCUT_ + (RandInt(ySpan * 10)) * 0.1f;
                float d = (RandInt(628)) * 0.01f;
                float f = 1.0f + (RandInt(80)) * 0.01f;
                ci_[x] = cosf(d) * f;
                cj_[x] = sinf(d) * f;
                ctype_[x] = RandInt(numPushTypes);
                cr_[x] = 80.0f + (RandInt(1200)) * 0.1f;
                cr_inv_[x] = 1.0f / cr_[x];
            }
        } else if (new_mode_ == 7) {
            turn1_ = 0.01f + 0.01f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        } else if (new_mode_ == 8) {
            turn1_ = 0.05f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        } else if (new_mode_ == 9) {
            scale1_ = 0.8f + 0.25f * (RandInt(1000) * 0.001f);
            scale1_ = (scale1_ - 1.0f) * protective_factor + 1.0f;
            scale2_ = scale1_;
            turn1_ = 0.01f + 0.03f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        } else if (new_mode_ == 10) {
            // no init
        } else if (new_mode_ == 11) {
            scale1_ = 1.008f + 0.008f * (RandInt(1000) * 0.001f);
            scale2_ = scale1_;
            turn1_ = 0.12f + 0.06f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
            turn1_ *= -0.6f;
            turn2_ *= 0.1f;
            scale1_ *= 0.99f;
            scale2_ *= 1.01f;
        } else if (new_mode_ == 12) {
            weightsum_ = static_cast<int>(weightsum_ * 0.98f);
        } else if (new_mode_ == 13 || new_mode_ == 14 || new_mode_ >= 16) {
            turn1_ = 0.007f + 0.02f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        } else if (new_mode_ == 15) {
            turn1_ = 0.04f * (RandInt(1000) * 0.001f) + 0.045f * (RandInt(1000) * 0.001f);
            turn2_ = turn1_;
        }

        if (RandInt(2) == 1) {
            turn1_ *= -1.0f;
            turn2_ *= -1.0f;
        }

        f1_ = 0.92f + 0.05f * (RandInt(1000) * 0.001f);
        f2_ = 0.0009f + 0.0012f * (RandInt(1000) * 0.001f);

        if (new_mode_ == 5) {
            f1_ = 0.05f + 0.05f * (RandInt(1000) * 0.001f) + 0.07f * (RandInt(1000) * 0.001f);
            f2_ = 0.99f - 0.01f * (RandInt(1000) * 0.001f) - 0.02f * (RandInt(1000) * 0.001f);
        } else if (new_mode_ == 7) {
            f1_ = 0.92f + 0.01f * (RandInt(1000) * 0.001f);
            f2_ = 0.0006f + 0.0005f * (RandInt(1000) * 0.001f);
        } else if (new_mode_ == 8) {
            f1_ = RandInt(1000) * 0.001f;
            f1_ *= f1_;
            f1_ *= f1_;
            f1_ *= 8.0f;
            f1_ += 1.5f;
        } else if (new_mode_ == 9) {
            f1_ = 0.98f + 0.01f * (RandInt(1000) * 0.001f);
            f2_ = 0.0009f + 0.0012f * (RandInt(1000) * 0.001f);
        } else if (new_mode_ == 13) {
            f1_ = 0.92f + 0.16f * (RandInt(1000) * 0.001f);
        } else if (new_mode_ == 15) {
            f1_ = static_cast<float>(RandInt(5) + 2);
            f2_ = 0.92f + 0.06f * (RandInt(1000) * 0.001f);
            f3_ = 0.05f + 0.05f * (RandInt(1000) * 0.001f);
        } else if (new_mode_ == 17) {
            f1_ = 0.01f + 0.09f * (RandInt(1000) * 0.001f);
            f2_ = 0.01f + 0.09f * (RandInt(1000) * 0.001f);
            f3_ = 0.01f + 0.09f * (RandInt(1000) * 0.001f);
            f4_ = 0.01f + 0.09f * (RandInt(1000) * 0.001f);
        }

        turn1_ *= 0.6f;
        turn2_ *= 0.6f;

        if (g_bLoadPreset_) {
            turn1_ = pendingPreset_.t1;
            turn2_ = pendingPreset_.t2;
            scale1_ = pendingPreset_.s1;
            scale2_ = pendingPreset_.s2;
            f1_ = pendingPreset_.f1;
            f2_ = pendingPreset_.f2;
            f3_ = pendingPreset_.f3;
            f4_ = pendingPreset_.f4;
        }

        cos_turn1_ = cosf(turn1_);
        sin_turn1_ = sinf(turn1_);
        cos_turn2_ = cosf(turn2_);
        sin_turn2_ = sinf(turn2_);

        int pixels = FXW_ * FXH_;
        if (pixels <= 320 * 240)       weightsum_res_adjusted_ = weightsum_ * 250 / 256;
        else if (pixels <= 400 * 300)  weightsum_res_adjusted_ = weightsum_ * 251 / 256;
        else if (pixels <= 512 * 384)  weightsum_res_adjusted_ = weightsum_ * 252 / 256;
        else if (pixels <= 800 * 600)  weightsum_res_adjusted_ = weightsum_ * 253 / 256;
        else if (pixels <= 1280 * 960) weightsum_res_adjusted_ = weightsum_ * 254 / 256;
        else                           weightsum_res_adjusted_ = weightsum_ * 255 / 256;

        frames_til_auto_switch_ = std::max(30, config_.framesTilAutoSwitch);
        if (fps_ >= 10.0f && fps_ < 120.0f) {
            frames_til_auto_switch_ = static_cast<int>(config_.framesTilAutoSwitch * fps_ / 30.0f);
            frames_til_auto_switch_ = std::max(15, frames_til_auto_switch_);
        }
    }

    if (y_map_pos_ < maxPos) {
        frames_crunching_this_mode_++;
        float new_damping_temp = new_damping_;
        if (fps_at_last_mode_switch_ >= 10.0f && fps_at_last_mode_switch_ <= 120.0f) {
            new_damping_temp *= 30.0f / fps_at_last_mode_switch_;
        }
        new_damping_temp = std::clamp(new_damping_temp, 0.08f, 1.0f);

        int end_pos = FXW_ * (FX_YCUT_ + static_cast<int>(
            (static_cast<float>(frames_crunching_this_mode_) / static_cast<float>(frames_til_auto_switch_)) *
            static_cast<float>(FXH_ - FX_YCUT_ * 2)));
        if (g_rush_map_ || end_pos > maxPos) {
            end_pos = maxPos;
        }

        int y = y_map_pos_ / FXW_;
        int x = y_map_pos_ % FXW_;
        int maxLookatPixel = FXW_ * (FXH_ - 2) - 2;
        int minLookatPixel = FXW_ + 1;

        for (; y_map_pos_ < end_pos; ++y_map_pos_) {
            float newx2 = static_cast<float>(x - new_gXC_);
            float newy2 = static_cast<float>(y - new_gYC_);
            float newx = static_cast<float>(x);
            float newy = static_cast<float>(y);

            if (new_mode_ <= 16) {
                half_FXW = 1.0f;

                if (new_mode_ == 3) {
                    scale1_ = 0.95f - (newy2 * (480.0f / static_cast<float>(FXH_))) * 0.0005f;
                } else if (new_mode_ == 4) {
                    float r = sqrtf(newx2 * newx2 + newy2 * newy2) * rmult;
                    scale1_ = 0.9f + r * 0.0025f * 0.14f;
                } else if (new_mode_ == 5) {
                    float r = sqrtf(newx2 * newx2 + newy2 * newy2) * (1.0f / 200.0f) * rmult;
                    if (effect_[EFF_NUCLIDE] == -1) r = sqrtf(r);
                    else r *= 1.7f;
                    scale1_ = f2_ - f1_ * r;
                    scale1_ = (scale1_ - 1.0f) * protective_factor + 1.0f;
                } else if (new_mode_ == 7) {
                    float r = sqrtf(newx2 * newx2 + newy2 * newy2) * f2_ * rmult;
                    scale1_ = (f1_ - r);
                    scale1_ = (scale1_ - 1.0f) * protective_factor + 1.0f;
                    scale1_ += rand_array_[rand_array_pos_++];
                    if (rand_array_pos_ >= 2345) rand_array_pos_ = 0;
                } else if (new_mode_ == 8) {
                    float r = sqrtf(newx2 * newx2 + newy2 * newy2) * rmult;
                    scale1_ = 0.85f + 0.1f * sinf(sqrtf(r) * f1_);
                } else if (new_mode_ == 9) {
                    float r = sqrtf(newx2 * newx2 + newy2 * newy2) * f2_ * rmult;
                    scale1_ = f1_ - r;
                    scale1_ = (scale1_ - 1.0f) * protective_factor + 1.0f;
                } else if (new_mode_ == 13) {
                    float r = sqrtf(newx2 * newx2 + newy2 * newy2) * rmult;
                    scale1_ = 1.04f - r * sqrtf(r) * 0.00025f * 0.14f;
                    scale1_ = (scale1_ - 1.0f) * f1_ + 1.0f;
                } else if (new_mode_ == 14) {
                    scale1_ = 0.9f + 0.2f * cosf(newy2 * 12.0f / static_cast<float>(FXH_ + (RandInt(1024)) / 1024.0f));
                } else if (new_mode_ == 15) {
                    scale1_ = f2_ + f3_ * sinf(atan2f(newy2, newx2) * f1_);
                } else if (new_mode_ == 16) {
                    float r = sqrtf(newx2 * newx2 + newy2 * newy2) * rmult;
                    scale1_ = 1.05f - r * r * 0.00025f * 0.09f;
                    if (scale1_ < -1.5f) scale1_ = -1.5f;
                }
            } else {
                half_FXW = 0.5f * refSpan;
                newx2 *= inv_FXW;
                newy2 *= inv_FXW;

                if (new_mode_ == 17) {
                    scale1_ = 0.97f - newy2 * newy2 * 0.40f;
                } else if (new_mode_ == 18) {
                    scale1_ = 0.97f - newx2 * newx2 * 0.40f;
                } else if (new_mode_ == 19) {
                    scale1_ = 1.04f - 0.25f * sqrtf(newx2 * newx2 + newy2 * newy2);
                } else if (new_mode_ == 20) {
                    scale1_ = 1.15f - sqrtf(std::max(0.01f, newy2 + 1.4f)) * 0.20f;
                } else if (new_mode_ == 21) {
                    scale1_ = 0.95f - (static_cast<int>(fabsf(newx2) * 10.0f)) * 0.03f
                                    - (static_cast<int>(fabsf(newy2) * 10.0f)) * 0.03f;
                } else if (new_mode_ == 22) {
                    scale1_ = 0.95f - (static_cast<int>(sqrtf(newx2 * newx2 + newy2 * newy2) * 10.0f)) * 0.04f;
                } else if (new_mode_ == 23) {
                    scale1_ = 0.95f - ((static_cast<int>(sqrtf(newx2 * newx2 + newy2 * newy2) * 20.0f)) % 4) * 0.12f;
                } else if (new_mode_ == 24) {
                    scale1_ = 0.96f;
                    turn1_ = 0.05f;
                    cos_turn1_ = cosf(turn1_);
                    sin_turn1_ = sinf(turn1_);
                } else if (new_mode_ == 25) {
                    scale1_ = 3.0f / (3.0f + sqrtf(newx2 * newx2 + newy2 * newy2));
                }
            }

            if (custom_motion_vectors_[new_mode_]) {
                if (new_mode_ == 6) {
                    float tx = 0.0f, ty = 0.0f, f = 0.0f;
                    for (int n = 0; n < 5; ++n) {
                        float dx = cx_[n] - x;
                        float dy = cy_[n] - y;
                        float xxyy = dx * dx + dy * dy;
                        float d = 1.0f / (xxyy + 0.1f);
                        f += d;
                        if (ctype_[n] == 0) {
                            tx += ci_[n] * d;
                            ty += cj_[n] * d;
                        } else if (ctype_[n] == 1) {
                            float z = 1.0f / (sqrtf(xxyy) + 0.01f);
                            d += d;
                            tx += d * (-dy) * z;
                            ty += d * (dx) * z;
                        } else {
                            float z = 1.0f / (sqrtf(xxyy) + 0.01f);
                            d += d;
                            tx += d * (dy) * z;
                            ty += d * (-dx) * z;
                        }
                    }
                    if (f > 0.000001f) {
                        f = 1.9f / f;
                        tx *= f;
                        ty *= f;
                    } else {
                        tx = 0.0f;
                        ty = 0.0f;
                    }
                    newx = x + tx - 0.1f;
                    newy = y + ty + 0.6f;
                } else if (new_mode_ == 10) {
                    newx = newx2 * (1.03f + 0.03f * (y / static_cast<float>(FXH_))) + new_gXC_;
                    newy = y * 1.04f;
                } else if (new_mode_ == 12) {
                    if (newx2 < -0.5f) newx = -sqrtf(-newx2) + new_gXC_ + 0.9f;
                    else if (newx2 > 0.5f) newx = sqrtf(newx2) + new_gXC_ - 0.9f;
                    else newx = static_cast<float>(new_gXC_);
                    newy = newy2 + new_gYC_;
                }
            } else if (rotation_dither_[new_mode_]) {
                if ((x % 2) == (y % 2)) {
                    float rx = newx2 * cos_turn1_ - newy2 * sin_turn1_;
                    float ry = newx2 * sin_turn1_ + newy2 * cos_turn1_;
                    newx = rx * scale1_ * half_FXW + new_gXC_;
                    newy = ry * scale1_ * half_FXW + new_gYC_;
                } else {
                    float rx = newx2 * cos_turn2_ - newy2 * sin_turn2_;
                    float ry = newx2 * sin_turn2_ + newy2 * cos_turn2_;
                    newx = rx * scale2_ * half_FXW + new_gXC_;
                    newy = ry * scale2_ * half_FXW + new_gYC_;
                }
            } else {
                float rx = newx2 * cos_turn1_ - newy2 * sin_turn1_;
                float ry = newx2 * sin_turn1_ + newy2 * cos_turn1_;
                newx = rx * scale1_ * half_FXW + new_gXC_;
                newy = ry * scale1_ * half_FXW + new_gYC_;
            }

            if (std::isnan(newx) || std::isinf(newx)) newx = static_cast<float>(x);
            if (std::isnan(newy) || std::isinf(newy)) newy = static_cast<float>(y);

            newx = x * (1.0f - new_damping_temp) + newx * new_damping_temp;
            newy = y * (1.0f - new_damping_temp) + newy * new_damping_temp;

            float spanX = static_cast<float>(FXW_ - 1);
            if (newx < 0.0f || newx >= spanX) {
                newx = fmodf(newx, spanX);
                if (newx < 0.0f) newx += spanX;
            }
            newy = std::clamp(newy, 1.0f, static_cast<float>(FXH_ - 3));

            int a = std::clamp(static_cast<int>(newx), 0, FXW_ - 2);
            int b = std::clamp(static_cast<int>(newy), 1, FXH_ - 3);
            int lookat = std::clamp(b * FXW_ + a, minLookatPixel, maxLookatPixel);

            float fx = std::clamp(newx - a, 0.0f, 1.0f);
            float fy = std::clamp(newy - b, 0.0f, 1.0f);
            float ws = static_cast<float>(weightsum_res_adjusted_);

            MapCell& cell = mapNext_[y_map_pos_];
            cell.w0 = static_cast<uint8_t>((1.0f - fx) * (1.0f - fy) * ws);
            cell.w1 = static_cast<uint8_t>((       fx) * (1.0f - fy) * ws);
            cell.w2 = static_cast<uint8_t>((1.0f - fx) * (       fy) * ws);
            cell.w3 = static_cast<uint8_t>((       fx) * (       fy) * ws);
            cell.lookat = lookat;

            x++;
            if (x == FXW_) {
                x = 0;
                y++;
            }
        }
    }

    // Wait for big beat if beat mode active, otherwise apply when map finishes
    if (y_map_pos_ == maxPos &&
        !g_rush_map_ &&
        bBeatMode_ &&
        config_.useBeatDetection &&
        !bBigBeat_) {
        fBigBeatThreshold_ -= 0.2f / static_cast<float>(std::max(1, frames_til_auto_switch_));
    } else if (y_map_pos_ == maxPos &&
               (g_rush_map_ || !bBeatMode_ || !config_.useBeatDetection || bBigBeat_)) {
        y_map_pos_ = -1;
        mode_ = new_mode_;
        damping_ = new_damping_;
        gXC_ = new_gXC_;
        gYC_ = new_gYC_;
        waveform_ = new_waveform_;

        if (fps_ >= 5.0f && fps_ <= 120.0f) {
            fps_at_last_mode_switch_ = fps_;
        }

        old_f1_ = f1_;
        old_f2_ = f2_;
        old_f3_ = f3_;
        old_f4_ = f4_;
        old_weightsum_ = weightsum_;
        old_turn1_ = turn1_;
        old_turn2_ = turn2_;
        old_scale1_ = scale1_;
        old_scale2_ = scale2_;
        bLocked_ = false;

        // Clear off-screen edges
        if (mode_ != 6) {
            int k = std::max(0, FX_YCUT_HIDE_ - 6);
            int lines = std::min(6, FX_YCUT_HIDE_);
            size_t bpp = (config_.colorDepthBits == 8) ? 1u : 4u;
            std::memset(&vs1_[k * FXW_ * bpp], 0, static_cast<size_t>(FXW_ * lines) * bpp);
            std::memset(&vs1_[(FXH_ - k - lines) * FXW_ * bpp], 0, static_cast<size_t>(FXW_ * lines) * bpp);
            std::memset(&vs2_[k * FXW_ * bpp], 0, static_cast<size_t>(FXW_ * lines) * bpp);
            std::memset(&vs2_[(FXH_ - k - lines) * FXW_ * bpp], 0, static_cast<size_t>(FXW_ * lines) * bpp);
        }

        if (g_bLoadPreset_) {
            for (int i = 0; i < NUM_EFFECTS; ++i) {
                effect_[i] = pendingPreset_.effects[i];
            }
            bSlideShift_ = pendingPreset_.shift;
            currentPalette_ = pendingPreset_.palette;
            if (config_.colorDepthBits == 8) {
                RandomPalette(true);
            }
        } else {
            bSlideShift_ = (RandInt(100) + 1) <= config_.slideShiftFreq;
            for (int i = 0; i < NUM_EFFECTS; ++i) {
                int thresh = modeInfo_[mode_].effect_freq[i];
                if (soundActive_ && !soundEmpty_) thresh = static_cast<int>(thresh * 0.7f);
                effect_[i] = (RandInt(1000) < thresh) ? 1 : -1;
            }
            // Clip effects within [min_effects, max_effects]
            int count = 0;
            for (int i = 0; i < NUM_EFFECTS; ++i) {
                if (effect_[i] > 0) count++;
            }
            while (count > modeInfo_[mode_].max_effects) {
                int idx = RandInt(NUM_EFFECTS);
                if (effect_[idx] > 0) {
                    effect_[idx] = -1;
                    count--;
                }
            }
            while (count < modeInfo_[mode_].min_effects) {
                int idx = RandInt(NUM_EFFECTS);
                if (effect_[idx] <= 0 && modeInfo_[mode_].effect_freq[idx] > 0) {
                    effect_[idx] = 1;
                    count++;
                } else {
                    break;
                }
            }

            if (effect_[EFF_CHASERS] == 1) {
                effect_[EFF_CHASERS] = 1 + RandInt(2);
            }
            if (effect_[EFF_GRID] == 1) {
                effect_[EFF_BAR] = -1;
            }
            if (mode_ == 1 && RandInt(2) == 0) {
                DropSolarParticles(500);
            }
            grid_dir_ = (RandInt(2) * 2) - 1;
            if (effect_[EFF_NUCLIDE] > 0 && RandInt(7) > 2) {
                waveform_ = 0;
            }
            visMode_ = VIS_WAVE;
            if (new_mode_ == 10) {
                waveform_ = 1;
                if (RandInt(4) > 0) visMode_ = VIS_SPECTRUM;
            }
            if (new_mode_ == 15 && RandInt(5) == 0) {
                waveform_ = 5;
            }
            if (effect_[EFF_SPECTRAL] == 1) {
                waveform_ = 0;
            }
        }

        if (!bSlideShift_) slider1_ = 0;

        solar_max_ = modeInfo_[mode_].solar_max;
        center_dwindle_ = modeInfo_[mode_].center_dwindle;

        std::swap(mapCurr_, mapNext_);

        if (!g_bLoadPreset_ && intframe_ > 0) {
            RandomPalette(false);
        }

        g_rush_map_ = false;
        g_bLoadPreset_ = false;
    }
}

void GeissEngine::ProcessMap() {
    const int startPixel = FX_YCUT_ * FXW_;
    const int endPixel = (FXH_ - FX_YCUT_) * FXW_;
    const int shift = bSlideShift_ ? slider1_ : 0;

    if (config_.colorDepthBits == 8) {
        const uint8_t* src = vs1_.data();
        uint8_t* dst = vs2_.data();
        const int maxSafe = FXW_x_FXH_ - FXW_ - 2;

        for (int i = startPixel; i < endPixel; ++i) {
            const MapCell& c = mapCurr_[i];
            int la = c.lookat + shift;
            if (shift != 0) {
                la = std::clamp(la, 0, maxSafe);
            }
            int val = src[la] * c.w0 +
                      src[la + 1] * c.w1 +
                      src[la + FXW_] * c.w2 +
                      src[la + FXW_ + 1] * c.w3;
            dst[i] = static_cast<uint8_t>(val >> 8);
        }
    } else {
        const uint8_t* src = vs1_.data();
        uint8_t* dst = vs2_.data();
        const int maxSafe = FXW_x_FXH_ - FXW_ - 2;
        const int rowBytes = FXW_ * 4;

        for (int i = startPixel; i < endPixel; ++i) {
            const MapCell& c = mapCurr_[i];
            int laPx = c.lookat + shift;
            if (shift != 0) {
                laPx = std::clamp(laPx, 0, maxSafe);
            }
            int la = laPx * 4;
            int d = i * 4;
            int w0 = c.w0, w1 = c.w1, w2 = c.w2, w3 = c.w3;

            int r = (src[la]     * w0 + src[la + 4]     * w1 + src[la + rowBytes]     * w2 + src[la + rowBytes + 4]     * w3) >> 8;
            int g = (src[la + 1] * w0 + src[la + 5]     * w1 + src[la + rowBytes + 1] * w2 + src[la + rowBytes + 5] * w3) >> 8;
            int b = (src[la + 2] * w0 + src[la + 6]     * w1 + src[la + rowBytes + 2] * w2 + src[la + rowBytes + 6] * w3) >> 8;

            dst[d]     = static_cast<uint8_t>(r);
            dst[d + 1] = static_cast<uint8_t>(g);
            dst[d + 2] = static_cast<uint8_t>(b);
            dst[d + 3] = 255;
        }
    }
}

void GeissEngine::ShadeBobs() {
    for (int x = 0; x < effect_[EFF_SHADE]; ++x) {
        int cr = static_cast<int>(1.0f + sinf(floatframe_ * micro_c1_[x]));
        int cg = static_cast<int>(1.0f + sinf(floatframe_ * micro_c2_[x]));
        int cb = static_cast<int>(1.0f + sinf(floatframe_ * micro_c3_[x]));

        int a = gXC_ + static_cast<int>(micro_rad_[0][x] * cosf(floatframe_ * micro_f1_[x]) +
                                          micro_rad_[2][x] * cosf(floatframe_ * micro_f2_[x]));
        int b = gYC_ + static_cast<int>(micro_rad_[1][x] * cosf(floatframe_ * micro_f3_[x]) +
                                          micro_rad_[3][x] * cosf(floatframe_ * micro_f4_[x]));

        for (int k = 0; k < 4; ++k) {
            b += RandInt(5) - 2;
            a += RandInt(5) - 2;
            if (a > 1 && a < FXW_ - 2 && b > FX_YCUT_ && b < FXH_ - 1 - FX_YCUT_) {
                int off = b * FXW_ + a;
                if (config_.colorDepthBits == 8) {
                    if (vs1_[off] < 250) vs1_[off] += 2;
                    if (vs1_[off + 1] < 250) vs1_[off + 1] += 1;
                    if (vs1_[off - 1] < 250) vs1_[off - 1] += 1;
                    if (vs1_[off + FXW_] < 250) vs1_[off + FXW_] += 1;
                    if (vs1_[off - FXW_] < 250) vs1_[off - FXW_] += 1;
                } else {
                    off *= 4;
                    int stride = FXW_ * 4;
                    int flags[3] = {cr, cg, cb};
                    for (int c = 0; c < 3; ++c) {
                        if (flags[c] == 1) {
                            if (vs1_[off + c] < 250) vs1_[off + c] += 5;
                            if (vs1_[off + c + 4] < 250) vs1_[off + c + 4] += 3;
                            if (vs1_[off + c - 4] < 250) vs1_[off + c - 4] += 3;
                            if (vs1_[off + c + stride] < 250) vs1_[off + c + stride] += 3;
                            if (vs1_[off + c - stride] < 250) vs1_[off + c - stride] += 3;
                        }
                    }
                }
            }
        }
    }
}

void GeissEngine::DiminishCenter() {
    if (center_dwindle_ >= 0.999f) return;

    bool bCenter = (mode_ != 12);
    if (bCenter) {
        int cx = std::clamp(gXC_, 1, FXW_ - 2);
        int cy = std::clamp(gYC_, 1, FXH_ - 2);
        int pts[5] = {
            cy * FXW_ + cx,
            cy * FXW_ + cx - 1,
            cy * FXW_ + cx + 1,
            (cy + 1) * FXW_ + cx,
            (cy - 1) * FXW_ + cx
        };
        if (config_.colorDepthBits == 8) {
            for (int p : pts) {
                if (vs1_[p] > 1) vs1_[p] = static_cast<uint8_t>(vs1_[p] * center_dwindle_);
            }
        } else {
            for (int p : pts) {
                int idx = p * 4;
                for (int c = 0; c < 3; ++c) {
                    if (vs1_[idx + c] > 1) {
                        vs1_[idx + c] = static_cast<uint8_t>(vs1_[idx + c] * center_dwindle_);
                    }
                }
            }
        }
    } else {
        int cx = std::clamp(gXC_, 1, FXW_ - 2);
        for (int i = FX_YCUT_; i < FXH_ - FX_YCUT_; ++i) {
            int base = i * FXW_ + cx;
            if (config_.colorDepthBits == 8) {
                vs1_[base - 1] = static_cast<uint8_t>(vs1_[base - 1] * center_dwindle_);
                vs1_[base]     = static_cast<uint8_t>(vs1_[base] * center_dwindle_);
                vs1_[base + 1] = static_cast<uint8_t>(vs1_[base + 1] * center_dwindle_);
            } else {
                int idx = base * 4;
                for (int dx = -4; dx <= 4; dx += 4) {
                    for (int c = 0; c < 3; ++c) {
                        vs1_[idx + dx + c] = static_cast<uint8_t>(vs1_[idx + dx + c] * center_dwindle_);
                    }
                }
            }
        }
    }
}

void GeissEngine::DropSolarParticles(int n) {
    if (config_.colorDepthBits == 8) n *= 4;
    int stride4 = FXW_ * 4;

    for (int k = n; k > 0; --k) {
        float dist = 100.0f;
        int x = 0, y = 0;
        for (int tries = 0; tries < 24 && dist >= 35.0f; ++tries) {
            y = RandInt(72) - 36;
            x = RandInt(96) - 48;
            dist = sqrtf(static_cast<float>(x * x + y * y));
        }
        if (dist >= 35.0f) continue;

        y += gYC_;
        x += gXC_;
        if (x > 1 && x < FXW_ - 2 && y > FX_YCUT_ && y < FXH_ - 1 - FX_YCUT_) {
            int off = y * FXW_ + x;
            if (config_.colorDepthBits == 8) {
                int i0 = 2 + RandInt(2) + static_cast<int>((35.0f - dist) / 9.0f);
                int i1 = std::max(0, i0 - 1);
                int i2 = std::max(0, i1 - 1);
                if (vs1_[off] < 207 - i0) {
                    vs1_[off] += i0;
                    vs1_[off + 1] += i1;
                    vs1_[off - 1] += i1;
                    vs1_[off + FXW_] += i1;
                    vs1_[off - FXW_] += i1;
                    vs1_[off - FXW_ - 1] += i2;
                    vs1_[off - FXW_ + 1] += i2;
                    vs1_[off + FXW_ - 1] += i2;
                    vs1_[off + FXW_ + 1] += i2;
                }
            } else {
                off *= 4;
                for (int c = 0; c < 3; ++c) {
                    int i0 = 4 + static_cast<int>(RandInt(30) * (35.0f - dist) / 25.0f);
                    int i1 = std::max(0, i0 - 3);
                    int i2 = i1 / 2;
                    if (vs1_[off + c] < 207 - i0) {
                        vs1_[off + c] += i0;
                        vs1_[off + c + 4] += i1;
                        vs1_[off + c - 4] += i1;
                        vs1_[off + c + stride4] += i1;
                        vs1_[off + c - stride4] += i1;
                        vs1_[off + c - stride4 - 4] += i2;
                        vs1_[off + c - stride4 + 4] += i2;
                        vs1_[off + c + stride4 - 4] += i2;
                        vs1_[off + c + stride4 + 4] += i2;
                    }
                }
            }
        }
    }
}

void GeissEngine::SolidLine(float frame, int channelOffset) {
    float s = FXW_ / 640.0f;
    float f = frame * 0.55f / (0.08f * 20.0f);
    float newx  = gXC_ + (s * 16.0f * cosf(f * 0.1102f + 10.0f) + s * 15.0f * cosf(f * 0.1312f + 20.0f));
    float newy  = gYC_ + (s * 15.0f * cosf(f * 0.1204f + 40.0f) + s * 10.0f * cosf(f * 0.1715f + 30.0f));
    float newx2 = gXC_ + (s * 14.0f * cosf(f * 0.1213f + 33.0f) + s * 13.0f * cosf(f * 0.1408f + 15.0f));
    float newy2 = gYC_ + (s * 13.0f * cosf(f * 0.1304f + 12.0f) + s * 11.0f * cosf(f * 0.1103f + 21.0f));

    int steps = std::max(16, static_cast<int>(s * 50.0f));
    float inv_s = 1.0f / static_cast<float>(steps);
    for (int k = 0; k < steps; ++k) {
        float t = k * inv_s;
        int a = static_cast<int>(newx * t + newx2 * (1.0f - t));
        int b = static_cast<int>(newy * t + newy2 * (1.0f - t));
        if (a >= 0 && a < FXW_ && b > FX_YCUT_ && b < FXH_ - 1 - FX_YCUT_) {
            int off = b * FXW_ + a;
            if (config_.colorDepthBits > 8) off = off * 4 + channelOffset;
            if (vs1_[off] < 223) vs1_[off] += 16;
        }
    }
}

void GeissEngine::TwoChasers(float frame) {
    float s = FXW_ / 640.0f;
    float t = frame;
    float time_scale = 1.0f;
    if (fps_at_last_mode_switch_ >= 10.0f && fps_at_last_mode_switch_ < 120.0f) {
        time_scale = 30.0f / fps_at_last_mode_switch_;
    }
    t *= time_scale;

    int passes = (effect_[EFF_CHASERS] == 2) ? 2 : 1;
    int k2 = std::max(8, static_cast<int>(20.0f * s));
    for (int k = 0; k < k2; ++k) {
        t += 0.08f * time_scale * 20.0f / static_cast<float>(k2);
        for (int pass = 0; pass < passes; ++pass) {
            int a, b;
            if (pass == 0) {
                a = gXC_ + static_cast<int>(s * 74.0f * cosf(t * 0.1102f + 10.0f) + s * 65.0f * cosf(t * 0.1312f + 20.0f));
                b = gYC_ + static_cast<int>(s * 54.0f * cosf(t * 0.1204f + 40.0f) + s * 55.0f * cosf(t * 0.1715f + 30.0f));
            } else {
                a = gXC_ + static_cast<int>(s * 64.0f * cosf(t * 0.1213f + 33.0f) + s * 55.0f * cosf(t * 0.1408f + 15.0f));
                b = gYC_ + static_cast<int>(s * 52.0f * cosf(t * 0.1304f + 12.0f) + s * 51.0f * cosf(t * 0.1103f + 21.0f));
            }

            if (a >= 0 && a < FXW_ && b > FX_YCUT_ && b < FXH_ - 1 - FX_YCUT_) {
                if (config_.colorDepthBits > 8) {
                    int off = (b * FXW_ + a) * 4;
                    vs1_[off]     = static_cast<uint8_t>(255 - (255 - vs1_[off]) * 0.6f);
                    vs1_[off + 1] = static_cast<uint8_t>(255 - (255 - vs1_[off + 1]) * 0.6f);
                    vs1_[off + 2] = static_cast<uint8_t>(255 - (255 - vs1_[off + 2]) * 0.6f);
                } else {
                    int off = b * FXW_ + a;
                    vs1_[off] = static_cast<uint8_t>(255 - (255 - vs1_[off]) * 0.6f);
                }
            }
        }
    }
}

void GeissEngine::Nuclide() {
    if (!soundActive_ || soundEmpty_) {
        if (RandInt(12) == 0) {
            int nodes = 3 + RandInt(5);
            int phase = RandInt(1000);
            int r = (FXW_ <= 320) ? (2 + RandInt(6)) : (3 + RandInt(8));
            int rad = (FXW_ <= 320) ? (22 + RandInt(6)) : (34 + RandInt(8));

            float cr = 1.0f, cg = 1.0f, cb = 1.0f;
            if (config_.colorDepthBits > 8) {
                long intfram = intframe_ + chaser_offset_;
                float f = 7.0f * sinf(intfram * 0.007f + 29.0f) + 5.0f * cosf(intfram * 0.0057f + 27.0f);
                cr = 0.5f + 0.25f * sinf(intfram * gF_[0] + 20.0f - f) + 0.25f * cosf(intfram * gF_[3] + 17.0f + f);
                cg = 0.5f + 0.25f * sinf(intfram * gF_[1] + 42.0f + f) + 0.25f * cosf(intfram * gF_[4] + 26.0f - f);
                cb = 0.5f + 0.25f * sinf(intfram * gF_[2] + 57.0f - f) + 0.25f * cosf(intfram * gF_[5] + 35.0f + f);
            }

            for (int n = 0; n < nodes; ++n) {
                int cx = gXC_ + static_cast<int>(rad * cosf(n / static_cast<float>(nodes) * 6.283185f + phase));
                int cy = gYC_ + static_cast<int>(rad * sinf(n / static_cast<float>(nodes) * 6.283185f + phase));
                if (cy - 10 > FX_YCUT_ && cy + 10 < FXH_ - 1 - FX_YCUT_ &&
                    cx - 10 >= 0 && cx + 10 < FXW_) {
                    for (int y = -10; y <= 10; ++y) {
                        for (int x = -10; x <= 10; ++x) {
                            int val = static_cast<int>((r - sqrt_tab_[x + 10][y + 10]) * 25.0f);
                            if (val > 0) {
                                int px = cx + x;
                                int py = cy + y;
                                if (config_.colorDepthBits == 8) {
                                    int idx = py * FXW_ + px;
                                    vs1_[idx] = static_cast<uint8_t>(std::min(255, vs1_[idx] + val));
                                } else {
                                    int idx = (py * FXW_ + px) * 4;
                                    vs1_[idx]     = static_cast<uint8_t>(std::min(255, vs1_[idx]     + static_cast<int>(val * cr)));
                                    vs1_[idx + 1] = static_cast<uint8_t>(std::min(255, vs1_[idx + 1] + static_cast<int>(val * cg)));
                                    vs1_[idx + 2] = static_cast<uint8_t>(std::min(255, vs1_[idx + 2] + static_cast<int>(val * cb)));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void GeissEngine::OneDottyChaser(float frame) {
    float s = FXW_ / 640.0f;
    float t = frame;
    if (fps_at_last_mode_switch_ >= 10.0f && fps_at_last_mode_switch_ < 120.0f) {
        t *= 30.0f / fps_at_last_mode_switch_;
    }

    int a = gXC_ + static_cast<int>(s * 64.0f * cosf(t * 0.0613f + 33.0f) + s * 55.0f * cosf(t * 0.0708f + 15.0f));
    int b = gYC_ + static_cast<int>(s * 52.0f * cosf(t * 0.0704f + 12.0f) + s * 51.0f * cosf(t * 0.0503f + 21.0f));

    if (a >= 1 && a < FXW_ - 2 && b > FX_YCUT_ && b < FXH_ - 1 - FX_YCUT_) {
        chaser_ptr_ = (chaser_ptr_ + 1) % 20;
        chaser_x_[chaser_ptr_] = a;
        chaser_y_[chaser_ptr_] = b;
        chaser_r_[chaser_ptr_] = static_cast<uint8_t>(127 + 126 * sinf(t * 0.0613f + 33.0f));
        chaser_g_[chaser_ptr_] = static_cast<uint8_t>(127 + 126 * sinf(t * 0.0713f + 30.0f));
        chaser_b_[chaser_ptr_] = static_cast<uint8_t>(127 + 126 * sinf(t * 0.0513f + 27.0f));

        for (int k = 0; k < 20; ++k) {
            int cx = chaser_x_[k];
            int cy = chaser_y_[k];
            if (cx >= 0 && cx < FXW_ - 1 && cy > FX_YCUT_ && cy < FXH_ - 1 - FX_YCUT_) {
                if (config_.colorDepthBits == 8) {
                    int off = cy * FXW_ + cx;
                    vs1_[off] = chaser_r_[k];
                    if (FXW_ >= 1050) {
                        vs1_[off + 1] = chaser_r_[k];
                        vs1_[off + FXW_] = chaser_r_[k];
                        vs1_[off + FXW_ + 1] = chaser_r_[k];
                    }
                } else {
                    int off = (cy * FXW_ + cx) * 4;
                    vs1_[off]     = chaser_r_[k];
                    vs1_[off + 1] = chaser_g_[k];
                    vs1_[off + 2] = chaser_b_[k];
                    if (FXW_ >= 880) {
                        vs1_[off + 4] = chaser_r_[k];
                        vs1_[off + 5] = chaser_g_[k];
                        vs1_[off + 6] = chaser_b_[k];
                    }
                }
            }
            chaser_x_[k]++;
        }
    }
}

void GeissEngine::Grid() {
    int xInc = std::max(8, FXW_ / 30);
    int yInc = xInc;
    float fpsScale = (fps_at_last_mode_switch_ > 1.0f) ? (30.0f / fps_at_last_mode_switch_) : 1.0f;
    float sv = 65.0f + 45.0f * sinf(intframe_ * 0.06033f * fpsScale)
                     + 35.0f * cosf(intframe_ * 0.04710f * fpsScale + 1.0f)
                     + 25.0f * cosf(intframe_ * 0.00523f * fpsScale - 1.0f);
    uint8_t s = static_cast<uint8_t>(std::clamp(sv, 0.0f, 255.0f));

    int dir = static_cast<int>(intframe_ % xInc);
    if (grid_dir_ == 1) dir *= -1;

    for (int y = FX_YCUT_HIDE_; y < FXH_ - FX_YCUT_HIDE_; y += yInc) {
        for (int x = 0; x < FXW_ - 1; x += xInc) {
            int px = x + dir;
            if (px < 0 || px >= FXW_) continue;
            if (config_.colorDepthBits == 8) {
                int k = y * FXW_ + px;
                if (vs1_[k] < s) vs1_[k] = s;
            } else {
                int k = (y * FXW_ + px) * 4;
                if (vs1_[k]     < s) vs1_[k]     = s;
                if (vs1_[k + 1] < s) vs1_[k + 1] = s;
                if (vs1_[k + 2] < s) vs1_[k + 2] = s;
            }
        }
    }
}

void GeissEngine::RenderFX() {
    floatframe_ += 1.6f * std::min(1.0f, 47.0f / std::max(5.0f, fps_));
    intframe_++;
    frames_this_mode_++;

    if ((intframe_ % 11) == 0) clearframes_ = 1;

    if (iBlendsLeftInPal_ > 0) {
        UpdatePaletteBlend();
    }

    if (effect_[EFF_SHADE] > 0) ShadeBobs();
    if (effect_[EFF_CHASERS] >= 1) TwoChasers(floatframe_ + static_cast<float>(chaser_offset_));
    if (effect_[EFF_BAR] == 1) {
        float speed_mult = 0.6f;
        float chromatic_dispersion = 4.0f;
        if (config_.colorDepthBits == 8) {
            SolidLine(floatframe_ + chaser_offset_ * speed_mult, 0);
        } else {
            SolidLine(floatframe_ + chaser_offset_ * speed_mult, 0);
            SolidLine(floatframe_ + chaser_offset_ * speed_mult + 3.5f * chromatic_dispersion * (sinf(floatframe_ * 0.03f + 1.0f) + cosf(floatframe_ * 0.04f + 3.0f)), 1);
            SolidLine(floatframe_ + chaser_offset_ * speed_mult - 3.5f * chromatic_dispersion * (cosf(floatframe_ * 0.05f + 2.0f) + sinf(floatframe_ * 0.06f + 4.0f)), 2);
        }
    }
    if (effect_[EFF_DOTS] == 1) OneDottyChaser(floatframe_);
    if (effect_[EFF_NUCLIDE] == 1) Nuclide();
    if (effect_[EFF_GRID] == 1) Grid();

    if (effect_[EFF_SOLAR] == 1) {
        if (FXW_ <= 320) {
            int fx = static_cast<int>(3 + solar_max_ * (2.4f + 0.35f * sinf(intframe_ * 0.05f) + 0.4f * sinf(intframe_ * 0.038f + 1.0f)));
            DropSolarParticles(static_cast<int>(fx * 0.01f));
        } else {
            int fx = static_cast<int>(3 + solar_max_ * 1.6f + solar_max_ * 0.43f * sinf(intframe_ * 0.05f) + solar_max_ * 0.43f * sinf(intframe_ * 0.038f + 1.0f));
            DropSolarParticles(static_cast<int>(fx * 0.05f));
        }
    }

    DiminishCenter();
}

void GeissEngine::FeedWaveAndSpectrum8(const uint8_t* waveL, const uint8_t* waveR, int waveLen,
                                       const uint8_t* specL, const uint8_t* specR, int specLen,
                                       bool active) {
    std::lock_guard<std::mutex> lock(audioMutex_);
    pendingAudioActive_ = active;
    lastAudioFeedMs_ = NowMs();

    if (waveL && waveLen > 0) {
        int n = std::min(waveLen, static_cast<int>(inWaveL_.size()));
        std::memcpy(inWaveL_.data(), waveL, static_cast<size_t>(n));
        if (n < static_cast<int>(inWaveL_.size())) {
            std::memset(inWaveL_.data() + n, 128, inWaveL_.size() - n);
        }
        if (waveR) {
            std::memcpy(inWaveR_.data(), waveR, static_cast<size_t>(n));
            if (n < static_cast<int>(inWaveR_.size())) {
                std::memset(inWaveR_.data() + n, 128, inWaveR_.size() - n);
            }
        } else {
            // Slight 4-sample decorrelation on R so stereo Lissajous & dual-wave look alive on mono Visualizer(0)
            for (int i = 0; i < n; ++i) {
                int j = (i + 4 < n) ? (i + 4) : i;
                inWaveR_[i] = waveL[j];
            }
        }
    }

    if (specL && specLen > 0) {
        int n = std::min(specLen, static_cast<int>(inSpecL_.size()));
        std::memcpy(inSpecL_.data(), specL, static_cast<size_t>(n));
        if (specR) {
            std::memcpy(inSpecR_.data(), specR, static_cast<size_t>(n));
        } else {
            std::memcpy(inSpecR_.data(), specL, static_cast<size_t>(n));
        }
    }
}

void GeissEngine::FeedPcm16Stereo(const int16_t* interleavedLR, int numFrames, bool active) {
    if (!interleavedLR || numFrames <= 0) {
        std::lock_guard<std::mutex> lock(audioMutex_);
        pendingAudioActive_ = false;
        return;
    }

    uint8_t waveL[1024];
    uint8_t waveR[1024];
    float fftIn[512];
    uint8_t spec[512];

    int n = std::min(numFrames, 1024);
    for (int i = 0; i < n; ++i) {
        int l = interleavedLR[i * 2] >> 8;
        int r = interleavedLR[i * 2 + 1] >> 8;
        waveL[i] = static_cast<uint8_t>(std::clamp(l + 128, 0, 255));
        waveR[i] = static_cast<uint8_t>(std::clamp(r + 128, 0, 255));
    }
    for (int i = n; i < 1024; ++i) {
        waveL[i] = 128;
        waveR[i] = 128;
    }

    int fftN = std::min(numFrames, 512);
    for (int i = 0; i < fftN; ++i) {
        fftIn[i] = 0.5f * (interleavedLR[i * 2] + interleavedLR[i * 2 + 1]) / 32768.0f;
    }
    for (int i = fftN; i < 512; ++i) {
        fftIn[i] = 0.0f;
    }
    ComputeFftMagnitudes(fftIn, 512, spec, 512);

    FeedWaveAndSpectrum8(waveL, waveR, n, spec, spec, 256, active);
}

void GeissEngine::GetWaveData() {
    uint8_t waveL[1024];
    uint8_t waveR[1024];
    uint8_t specL[512];
    uint8_t specR[512];
    bool active = false;
    uint64_t ageMs = 0;

    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        std::memcpy(waveL, inWaveL_.data(), sizeof(waveL));
        std::memcpy(waveR, inWaveR_.data(), sizeof(waveR));
        std::memcpy(specL, inSpecL_.data(), sizeof(specL));
        std::memcpy(specR, inSpecR_.data(), sizeof(specR));
        active = pendingAudioActive_;
        ageMs = NowMs() - lastAudioFeedMs_;
    }

    if (ageMs > 1000) {
        active = false;
    }

    // Optional gentle demo waveform if user explicitly enables demo when silent
    if (!active && config_.autoDemoWhenSilent) {
        active = true;
        float t = floatframe_ * 0.05f;
        for (int i = 0; i < 1024; ++i) {
            float s1 = sinf(i * 0.045f + t) * 42.0f + sinf(i * 0.11f - t * 1.7f) * 24.0f;
            float s2 = cosf(i * 0.042f - t * 0.8f) * 42.0f + sinf(i * 0.09f + t * 1.3f) * 24.0f;
            waveL[i] = static_cast<uint8_t>(std::clamp(128.0f + s1, 0.0f, 255.0f));
            waveR[i] = static_cast<uint8_t>(std::clamp(128.0f + s2, 0.0f, 255.0f));
        }
        for (int i = 0; i < 256; ++i) {
            float sp = std::max(0.0f, 120.0f * expf(-i * 0.015f) * (0.6f + 0.4f * sinf(t * 2.0f + i * 0.15f)));
            specL[i] = specR[i] = static_cast<uint8_t>(std::clamp(sp, 0.0f, 255.0f));
        }
    }

    soundActive_ = active;
    if (!soundReady_ || !soundActive_) {
        soundEmpty_ = true;
        current_vol_ = 0.0f;
        return;
    }

    float fDiv = 1.0f / (64.0f * (640.0f / static_cast<float>(FXW_)));
    float billy = volscale_ * fDiv;

    int y = 5;
    if (visMode_ != VIS_SPECTRUM) {
        int fxwDiv4 = std::min(FXW_ / 4, 140);
        y += fxwDiv4;
        int maxTrigger = 1024 - 380;
        while (y < maxTrigger) {
            int old_v = static_cast<int>(waveL[y - 5]) - 128;
            int v     = static_cast<int>(waveL[y]) - 128;
            if (std::abs(v - last_frame_v_) <= 1 &&
                ((last_frame_slope_ > 0 && (v - old_v) > 0) ||
                 (last_frame_slope_ <= 0 && (v - old_v) <= 0))) {
                last_frame_slope_ = v - old_v;
                last_frame_v_ = v;
                break;
            }
            y++;
        }
        if (y >= maxTrigger) {
            y = 5 + fxwDiv4;
            int old_v = static_cast<int>(waveL[y - 5]) - 128;
            int v     = static_cast<int>(waveL[y]) - 128;
            last_frame_slope_ = v - old_v;
            last_frame_v_ = v;
        }
        y -= fxwDiv4;
        if (y < 0) y = 0;
    }

    int halfBuf = BUFSIZE_ / 2;
    if (visMode_ == VIS_SPECTRUM) {
        for (int x = 0; x < halfBuf; ++x) {
            int si = (x * 256) / std::max(1, halfBuf);
            si = std::clamp(si, 0, 255);
            soundBuffer_[x * 2]     = static_cast<int16_t>((128 - static_cast<int>(specL[si])) * 256);
            soundBuffer_[x * 2 + 1] = static_cast<int16_t>((128 - static_cast<int>(specR[si])) * 256);
        }
    } else {
        int avail = 1024 - y;
        for (int x = 0; x < halfBuf; ++x) {
            int wi = y + (x * std::min(avail, 512)) / std::max(1, halfBuf);
            wi = std::clamp(wi, 0, 1023);
            soundBuffer_[x * 2]     = static_cast<int16_t>((static_cast<int>(waveL[wi]) - 128) * 256);
            soundBuffer_[x * 2 + 1] = static_cast<int16_t>((static_cast<int>(waveR[wi]) - 128) * 256);
        }
    }

    for (int i = 0; i < BUFSIZE_ - 2; ++i) {
        fSoundBuffer_[i] = (0.8f * soundBuffer_[i] + 0.2f * soundBuffer_[i + 2]) * billy;
    }
    fSoundBuffer_[BUFSIZE_ - 2] = soundBuffer_[BUFSIZE_ - 2] * billy;
    fSoundBuffer_[BUFSIZE_ - 1] = soundBuffer_[BUFSIZE_ - 1] * billy;

    if (visMode_ != VIS_SPECTRUM) {
        float c0 = 0.0f, c1 = 0.0f;
        int count0 = 0, count1 = 0;
        for (int i = 0; i < BUFSIZE_; i += 8) { c0 += fSoundBuffer_[i]; count0++; }
        for (int i = 1; i < BUFSIZE_; i += 8) { c1 += fSoundBuffer_[i]; count1++; }
        if (count0 > 0) c0 /= static_cast<float>(count0);
        if (count1 > 0) c1 /= static_cast<float>(count1);
        for (int i = 0; i < BUFSIZE_; i += 2) fSoundBuffer_[i]     -= c0;
        for (int i = 1; i < BUFSIZE_; i += 2) fSoundBuffer_[i] -= c1;
    }
}

void GeissEngine::RenderDots() {
    if (!soundReady_ || !soundActive_) return;

    int16_t low = soundBuffer_[0];
    int16_t high = soundBuffer_[0];
    for (int i = BUFSIZE_ - 4; i > 0; i -= 3) {
        if (soundBuffer_[i] < low) low = soundBuffer_[i];
        if (soundBuffer_[i] > high) high = soundBuffer_[i];
    }

    float vol = static_cast<float>(high - low) / 256.0f;
    past_vol_pos_ = (past_vol_pos_ + 1) % PAST_VOL_N;
    past_vol_[past_vol_pos_] = current_vol_;

    current_vol_ = vol;
    avg_vol_narrow_ = avg_vol_ * 0.3f + vol * 0.7f;
    avg_vol_        = avg_vol_ * 0.85f + vol * 0.15f;
    avg_vol_wide_   = avg_vol_wide_ * 0.96f + vol * 0.04f;

    if (vol <= 0.5f) {
        framesSinceSilence_++;
        if (framesSinceSilence_ > std::max(30, frames_til_auto_switch_ / 4)) {
            soundEmpty_ = true;
        }
    } else {
        framesSinceSilence_ = 0;
        soundEmpty_ = false;
    }

    if (!config_.disableSongTitlePopups && g_hit_ == -1 && !currentSongTitle_.empty()) {
        int freq = config_.randomSongTitleFreq;
        if (freq > 0 && RandInt(7000) < freq * freq) {
            g_hit_ = static_cast<int>(config_.songTooltipFrames * fps_at_last_mode_switch_ / 30.0f);
            g_title_R_ = static_cast<uint8_t>(128 + RandInt(99));
            g_title_G_ = static_cast<uint8_t>(128 + RandInt(99));
            g_title_B_ = static_cast<uint8_t>(128 + RandInt(99));
        }
    }

    if (effect_[EFF_NUCLIDE] > 0 && !soundEmpty_) {
        if (vol > avg_vol_narrow_ * 1.1f) {
            int nodes = 3 + RandInt(5);
            int phase = RandInt(1000);
            float ratio = vol / std::max(0.1f, avg_vol_narrow_);
            int r = (FXW_ <= 320) ? static_cast<int>(2 + 40 * (ratio - 1.1f))
                                  : static_cast<int>(3 + 40 * (ratio - 1.1f));
            r = std::clamp(r, 1, (FXW_ <= 320) ? 7 : 10);

            int rad = (FXW_ <= 320) ? (22 + RandInt(6)) : (34 + RandInt(8));
            if (FXW_ > 1024) rad = static_cast<int>(rad * FXW_ / 1024.0f);

            float cr = 1.0f, cg = 1.0f, cb = 1.0f;
            if (config_.colorDepthBits > 8) {
                long intfram = intframe_ + chaser_offset_;
                float f = 7.0f * sinf(intfram * 0.007f + 29.0f) + 5.0f * cosf(intfram * 0.0057f + 27.0f);
                cr = 0.58f + 0.21f * sinf(intfram * gF_[0] + 20.0f - f) + 0.21f * cosf(intfram * gF_[3] + 17.0f + f);
                cg = 0.58f + 0.21f * sinf(intfram * gF_[1] + 42.0f + f) + 0.21f * cosf(intfram * gF_[4] + 26.0f - f);
                cb = 0.58f + 0.21f * sinf(intfram * gF_[2] + 57.0f - f) + 0.21f * cosf(intfram * gF_[5] + 35.0f + f);
            }

            for (int n = 0; n < nodes; ++n) {
                int cx = gXC_ + static_cast<int>(rad * cosf(n / static_cast<float>(nodes) * 6.283185f + phase));
                int cy = gYC_ + static_cast<int>(rad * sinf(n / static_cast<float>(nodes) * 6.283185f + phase));
                for (int y = -10; y <= 10; ++y) {
                    int py = cy + y;
                    if (py <= FX_YCUT_ || py >= FXH_ - 1 - FX_YCUT_) continue;
                    for (int x = -10; x <= 10; ++x) {
                        int px = cx + x;
                        if (px < 0 || px >= FXW_) continue;
                        int val = static_cast<int>((r - sqrt_tab_[x + 10][y + 10]) * 25.0f);
                        if (val > 0) {
                            if (config_.colorDepthBits == 8) {
                                int idx = py * FXW_ + px;
                                vs2_[idx] = static_cast<uint8_t>(std::min(255, vs2_[idx] + val));
                            } else {
                                int idx = (py * FXW_ + px) * 4;
                                vs2_[idx]     = static_cast<uint8_t>(std::min(255, vs2_[idx]     + static_cast<int>(val * cr)));
                                vs2_[idx + 1] = static_cast<uint8_t>(std::min(255, vs2_[idx + 1] + static_cast<int>(val * cg)));
                                vs2_[idx + 2] = static_cast<uint8_t>(std::min(255, vs2_[idx + 2] + static_cast<int>(val * cb)));
                            }
                        }
                    }
                }
            }
        }
    }
}

void GeissEngine::RenderWave() {
    if (!soundReady_ || !soundActive_ || soundEmpty_) return;

    int base = static_cast<int>((current_vol_ * 4.0f + avg_vol_ * 0.4f) - 10.0f);

    // Beat detection
    past_vol_[past_vol_pos_] = avg_vol_narrow_;
    float avg_vol_uniform = 0.0f;
    for (int i = 0; i < PAST_VOL_N; ++i) avg_vol_uniform += past_vol_[i];
    avg_vol_uniform /= static_cast<float>(PAST_VOL_N);

    float avg_lo = 0.0f, avg_hi = 0.0f;
    int n_lo = 0, n_hi = 0;
    for (int i = 0; i < PAST_VOL_N; ++i) {
        if (past_vol_[i] < avg_vol_uniform) { avg_lo += past_vol_[i]; n_lo++; }
        else                                { avg_hi += past_vol_[i]; n_hi++; }
    }
    if (n_lo > 0) avg_lo /= n_lo;
    if (n_hi > 0) avg_hi /= n_hi;
    float beat_strength = (avg_hi - avg_lo);
    if (beat_strength > 109.0f) bBeatMode_ = true;
    if (beat_strength < 71.0f)  bBeatMode_ = false;

    float variance = 0.0f;
    for (int i = 0; i < PAST_VOL_N; ++i) {
        float d = past_vol_[i] - avg_vol_uniform;
        variance += d * d;
    }
    variance /= static_cast<float>(PAST_VOL_N - 1);
    float std_dev = sqrtf(std::max(0.0001f, variance));

    float max_vol = 0.0f;
    for (int i = 0; i < PAST_VOL_N / 3; ++i) max_vol = std::max(max_vol, past_vol_[i]);
    bBigBeat_ = (avg_vol_narrow_ > max_vol * fBigBeatThreshold_);

    float brite_scale = std::clamp((current_vol_ - avg_vol_uniform) / (std_dev * 0.5f), 0.0f, 1.0f);

    if (bBeatMode_ && config_.useBeatDetection && visMode_ != VIS_SPECTRUM && waveform_ != 6) {
        base = static_cast<int>(base * brite_scale);
    }

    // Screen shifting on beat
    framesSinceShift_++;
    if (bBeatMode_ && bSlideShift_) {
        if (current_vol_ > shiftMaxVol_) {
            shiftMaxVol_ = current_vol_ * 1.05f;
            if (framesSinceShift_ > config_.slideShiftMinFrames) {
                framesSinceShift_ = 0;
                int oldSlider = slider1_;
                slider1_ = (RandInt(std::max(1, FXW_ / 2)) + 50) / 145;
                if (oldSlider > 0) slider1_ *= -1;
                slider1_ += (RandInt(3) - 1) * FXW_;
            }
        }
        shiftMaxVol_ *= 0.99f;
    }

    base = std::clamp(base, 0, 155);

    uint8_t r = static_cast<uint8_t>(base);
    uint8_t g = static_cast<uint8_t>(base);
    uint8_t b = static_cast<uint8_t>(base);
    if (config_.colorDepthBits > 8) {
        long intfram = intframe_ + chaser_offset_;
        float f = 7.0f * sinf(intfram * 0.006f + 59.0f) + 5.0f * cosf(intfram * 0.0077f + 17.0f);
        int ir = static_cast<int>(base * 1.07f * (1.0f + 0.3f * sinf(intfram * gF_[0] + 10.0f - f)) * (1.0f + 0.2f * cosf(intfram * gF_[1] + 37.0f + f)));
        int ig = static_cast<int>(base * 1.07f * (1.0f + 0.3f * sinf(intfram * gF_[2] + 32.0f + f)) * (1.0f + 0.2f * cosf(intfram * gF_[3] + 16.0f - f)));
        int ib = static_cast<int>(base * 1.07f * (1.0f + 0.3f * sinf(intfram * gF_[4] + 87.0f - f)) * (1.0f + 0.2f * cosf(intfram * gF_[5] + 25.0f + f)));
        r = static_cast<uint8_t>(std::clamp(ir, 0, 255));
        g = static_cast<uint8_t>(std::clamp(ig, 0, 255));
        b = static_cast<uint8_t>(std::clamp(ib, 0, 255));
    }

    auto plot = [&](int px, int py) {
        if (px < 0 || px >= FXW_ || py < FX_YCUT_HIDE_ || py >= FXH_ - FX_YCUT_HIDE_) return;
        int off = py * FXW_ + px;
        if (config_.colorDepthBits == 8) {
            if (vs2_[off] < r) vs2_[off] = r;
        } else {
            off *= 4;
            if (vs2_[off]     < r) vs2_[off]     = r;
            if (vs2_[off + 1] < g) vs2_[off + 1] = g;
            if (vs2_[off + 2] < b) vs2_[off + 2] = b;
        }
    };

    if (waveform_ == 1) {
        float y_center = FXH_ * 0.5f;
        int start = 0;
        int end = FXW_;
        if (mode_ == 10) {
            y_center = ((FXH_ - FX_YCUT_) + (FXH_ * 0.5f)) * 0.5f;
            if (visMode_ == VIS_SPECTRUM) {
                y_center = 0.9f * (FXH_ - FX_YCUT_) + 0.1f * (FXH_ * 0.5f);
            }
            start += 10;
            end -= 10;
            if (FXW_ >= 640) { start += 5; end -= 5; }
        }
        float zL = fSoundBuffer_[start ^ (start & 1)] + y_center;
        for (int i = start; i < end; ++i) {
            float prev_zL = zL;
            zL = fSoundBuffer_[i ^ (i & 1)] + y_center;
            zL = prev_zL * 0.9f + zL * 0.1f;
            plot(i, static_cast<int>(zL));
        }
    } else if (waveform_ == 2) {
        float fDiv = 0.7f;
        float h1 = FXH_ * 0.4f;
        float h2 = FXH_ * 0.6f;
        float zL = fSoundBuffer_[0] * fDiv + h1;
        float zR = fSoundBuffer_[1] * fDiv + h2;
        for (int i = 0; i < FXW_; ++i) {
            zL = zL * 0.9f + (fSoundBuffer_[i ^ (i & 1)] * fDiv + h1) * 0.1f;
            zR = zR * 0.9f + (fSoundBuffer_[(i ^ (i & 1)) + 1] * fDiv + h2) * 0.1f;
            plot(i, static_cast<int>(zL));
            plot(i, static_cast<int>(zR));
        }
    } else if (waveform_ == 3) {
        float zL = fSoundBuffer_[FX_YCUT_HIDE_ ^ (FX_YCUT_HIDE_ & 1)] + FXW_ * 0.5f;
        for (int i = FX_YCUT_HIDE_; i < FXH_ - FX_YCUT_HIDE_; ++i) {
            zL = zL * 0.9f + (fSoundBuffer_[i ^ (i & 1)] + FXW_ * 0.5f) * 0.1f;
            plot(static_cast<int>(zL), i);
        }
    } else if (waveform_ == 4) {
        float fDiv = 0.9f;
        float zL = fSoundBuffer_[FX_YCUT_HIDE_ ^ (FX_YCUT_HIDE_ & 1)] * fDiv;
        float zR = fSoundBuffer_[(FX_YCUT_HIDE_ ^ (FX_YCUT_HIDE_ & 1)) + 1] * fDiv;
        float slope = static_cast<float>(FXW_) / static_cast<float>(std::max(1, FXH_));
        for (int i = FX_YCUT_HIDE_; i < FXH_ - FX_YCUT_HIDE_; ++i) {
            zL = zL * 0.9f + (fSoundBuffer_[i ^ (i & 1)] * fDiv) * 0.1f;
            zR = zR * 0.9f + (fSoundBuffer_[(i ^ (i & 1)) + 1] * fDiv) * 0.1f;
            int diag = static_cast<int>(i * slope);
            plot(static_cast<int>(zL) + diag - FXW_ / 8, i);
            plot(static_cast<int>(zR) + diag + FXW_ / 8, i);
        }
    } else if (waveform_ == 5) {
        float range_inv = 1.0f / WAVE_5_BLEND_RANGE;
        for (int i = 0; i < WAVE_5_BLEND_RANGE; ++i) {
            float amt = i * range_inv;
            fSoundBuffer_[i ^ (i & 1)] = fSoundBuffer_[i ^ (i & 1)] * amt +
                                         (1.0f - amt) * fSoundBuffer_[(i + 314) ^ ((i + 314) & 1)];
        }
        float base_rad = (FXW_ <= 320) ? 40.0f : (std::min(FXW_, FXH_) * 0.14f);
        float fDiv = 0.7f;
        float rad = base_rad + fSoundBuffer_[0] * fDiv;
        for (int i = 0; i < 314; ++i) {
            rad = rad * 0.5f + 0.5f * (base_rad + fSoundBuffer_[i ^ (i & 1)] * fDiv);
            if (rad >= 5.0f) {
                int px = gXC_ + static_cast<int>(rad * cosf(i * 0.02f));
                int py = gYC_ + static_cast<int>(rad * sinf(i * 0.02f));
                plot(px, py);
            }
        }
    } else if (waveform_ == 6) {
        float range_inv = 1.0f / WAVE_5_BLEND_RANGE;
        for (int i = 0; i < WAVE_5_BLEND_RANGE; ++i) {
            float amt = i * range_inv;
            fSoundBuffer_[i ^ (i & 1)] = fSoundBuffer_[i ^ (i & 1)] * amt +
                                         (1.0f - amt) * fSoundBuffer_[(i + 314) ^ ((i + 314) & 1)];
        }
        float ang = sinf(intframe_ * 0.01f);
        float ca = cosf(ang), sa = sinf(ang);
        for (int i = 0; i < 314; ++i) {
            float px2 = fSoundBuffer_[i ^ (i & 1)];
            float py2 = fSoundBuffer_[(i ^ (i & 1)) + 1];
            int px = gXC_ + static_cast<int>(px2 * ca + py2 * sa);
            int py = gYC_ + static_cast<int>(-px2 * sa + py2 * ca);
            plot(px, py);
        }
    }
}

float GeissEngine::CrankPal(unsigned int curve_id, int z) const {
    float x = static_cast<float>(z);
    switch (curve_id) {
        case 1: return sqrtf(x) * 22.6f;
        case 2: return x * 2.0f;
        case 3: return x * x / 64.0f;
        case 4: return 255.0f * sinf(x / 256.0f * 0.5f * kPi);
        case 5: return x * 3.5f;
        case 6: return powf(1.5f, x / 20.0f) - 1.0f;
        case 7: return x * 1.5f + 128.0f * 0.25f + 128.0f * 0.25f * sinf(z * 0.3f);
        default: return 255.0f;
    }
}

void GeissEngine::RandomPalette(bool loadPal) {
    if (bPalLocked_) return;
    if (config_.colorDepthBits != 8) return;

    if (!loadPal) {
        currentPalette_.lo_band = -1;
        currentPalette_.hi_band = -1;
        currentPalette_.bFXPalette = false;
        currentPalette_.iFXPaletteNum = -1;
        currentPalette_.c1 = -1;
        currentPalette_.c2 = -1;
        currentPalette_.c3 = -1;

        if (RandInt(10) < config_.coarsePalFreq) {
            currentPalette_.lo_band = 7 + RandInt(6);
            currentPalette_.hi_band = 17 + RandInt(6);
        }
    }

    iBlendsLeftInPal_ = 18;
    int b = loadPal ? (currentPalette_.bFXPalette ? 0 : 1) : RandInt(6);

    if (b == 0) {
        currentPalette_.bFXPalette = true;
        if (!loadPal) currentPalette_.iFXPaletteNum = RandInt(4);

        uint8_t rmap[256], gmap[256], bmap[256];
        for (int a = 0; a < 128; ++a) {
            uint8_t sq = static_cast<uint8_t>(std::min(255.0f, a * a / 64.0f));
            uint8_t lin = static_cast<uint8_t>(std::min(255, a * 2));
            uint8_t rt = static_cast<uint8_t>(std::min(255.0f, sqrtf(static_cast<float>(a)) * 22.6f));
            switch (currentPalette_.iFXPaletteNum) {
                case 0: rmap[a] = sq;  bmap[a] = lin; gmap[a] = rt;  break;
                case 1: rmap[a] = sq;  bmap[a] = rt;  gmap[a] = lin; break;
                case 2: rmap[a] = rt;  bmap[a] = lin; gmap[a] = sq;  break;
                default: rmap[a] = lin; bmap[a] = sq;  gmap[a] = rt;  break;
            }
        }
        for (int n = 128; n < 256; ++n) {
            rmap[n] = rmap[127];
            gmap[n] = gmap[127];
            bmap[n] = bmap[127];
        }
        for (int n = 0; n < 256; ++n) {
            ape_[n] = ape2_[n];
            ape2_[n].r = rmap[n];
            ape2_[n].g = gmap[n];
            ape2_[n].b = bmap[n];
        }
    } else {
        if (!loadPal) {
            int temp = 0;
            do {
                if (RandInt(5) < config_.solarPalFreq) {
                    currentPalette_.c1 = RandInt(7) + 1;
                    currentPalette_.c2 = RandInt(7) + 1;
                    currentPalette_.c3 = RandInt(7) + 1;
                } else {
                    currentPalette_.c1 = RandInt(6) + 1;
                    currentPalette_.c2 = RandInt(6) + 1;
                    currentPalette_.c3 = RandInt(6) + 1;
                }
                temp = 0;
                if (currentPalette_.c1 == 6) temp++;
                if (currentPalette_.c2 == 6) temp++;
                if (currentPalette_.c3 == 6) temp++;
            } while (temp > 1);
        }

        float gamma_factor = 1.0f + config_.gamma * 0.01f;
        if (soundEmpty_) gamma_factor += 0.3f;

        for (int n = 0; n < 256; ++n) {
            ape_[n] = ape2_[n];
            float x = CrankPal(currentPalette_.c1, n) * gamma_factor;
            float y = CrankPal(currentPalette_.c2, n) * gamma_factor;
            float z = CrankPal(currentPalette_.c3, n) * gamma_factor;

            if (n > currentPalette_.lo_band && n < currentPalette_.hi_band) {
                x *= 2.0f;
                y *= 2.0f;
                z *= 2.0f;
            }
            ape2_[n].r = static_cast<uint8_t>(std::min(255.0f, x));
            ape2_[n].b = static_cast<uint8_t>(std::min(255.0f, y));
            ape2_[n].g = static_cast<uint8_t>(std::min(255.0f, z));
        }
    }
}

void GeissEngine::UpdatePaletteBlend() {
    if (config_.colorDepthBits != 8 || iBlendsLeftInPal_ <= 0) return;
    iBlendsLeftInPal_--;
    float x = iBlendsLeftInPal_ / 18.0f;
    float y = 1.0f - x;
    for (int n = 0; n < 256; ++n) {
        apetemp_[n].r = static_cast<uint8_t>(ape_[n].r * x + ape2_[n].r * y);
        apetemp_[n].g = static_cast<uint8_t>(ape_[n].g * x + ape2_[n].g * y);
        apetemp_[n].b = static_cast<uint8_t>(ape_[n].b * x + ape2_[n].b * y);
    }
}

void GeissEngine::CompositeAndStampText() {
    if (g_hit_ < 2 || overlayRgba_.empty() || overlayW_ <= 0 || overlayH_ <= 0) {
        if (g_hit_ >= 0) g_hit_--;
        return;
    }

    if (g_hit_ == 2) {
        // Stamp text into VS1 so Geiss blurs and dissolves it into the plasma!
        for (int row = 0; row < overlayH_; ++row) {
            int dy = overlayY_ + row;
            if (dy < FX_YCUT_HIDE_ || dy >= FXH_ - FX_YCUT_HIDE_) continue;
            for (int col = 0; col < overlayW_; ++col) {
                int dx = overlayX_ + col;
                if (dx < 0 || dx >= FXW_) continue;
                uint8_t alpha = overlayRgba_[(row * overlayW_ + col) * 4 + 3];
                if (alpha > 40) {
                    if (config_.colorDepthBits == 8) {
                        vs1_[dy * FXW_ + dx] = 225;
                    } else {
                        int idx = (dy * FXW_ + dx) * 4;
                        vs1_[idx]     = g_title_R_;
                        vs1_[idx + 1] = g_title_G_;
                        vs1_[idx + 2] = g_title_B_;
                    }
                }
            }
        }
    } else {
        // Composite crisp shadow + foreground text onto rgbaOut_
        for (int row = 0; row < overlayH_; ++row) {
            int dy = overlayY_ + row;
            if (dy < 0 || dy >= FXH_) continue;
            for (int col = 0; col < overlayW_; ++col) {
                int dx = overlayX_ + col;
                if (dx < 0 || dx >= FXW_) continue;
                const uint8_t* src = &overlayRgba_[(row * overlayW_ + col) * 4];
                uint8_t a = src[3];
                if (a == 0) continue;
                float af = a / 255.0f;
                uint8_t* dst = &rgbaOut_[(dy * FXW_ + dx) * 4];
                dst[0] = static_cast<uint8_t>(dst[0] * (1.0f - af) + src[0] * af);
                dst[1] = static_cast<uint8_t>(dst[1] * (1.0f - af) + src[1] * af);
                dst[2] = static_cast<uint8_t>(dst[2] * (1.0f - af) + src[2] * af);
                dst[3] = 255;
            }
        }
    }

    g_hit_--;
}

void GeissEngine::ConvertToRgba() {
    int yStart = FX_YCUT_HIDE_;
    int yEnd = FXH_ - FX_YCUT_HIDE_;

    // Letterbox top/bottom clear
    if (yStart > 0) {
        std::memset(rgbaOut_.data(), 0, static_cast<size_t>(yStart * FXW_) * 4);
        for (int i = 0; i < yStart * FXW_; ++i) rgbaOut_[i * 4 + 3] = 255;
    }
    if (yEnd < FXH_) {
        int tailStart = yEnd * FXW_;
        int tailPx = (FXH_ - yEnd) * FXW_;
        std::memset(&rgbaOut_[tailStart * 4], 0, static_cast<size_t>(tailPx) * 4);
        for (int i = tailStart; i < FXW_x_FXH_; ++i) rgbaOut_[i * 4 + 3] = 255;
    }

    if (config_.colorDepthBits == 8) {
        for (int i = yStart * FXW_; i < yEnd * FXW_; ++i) {
            const PaletteEntry& pe = apetemp_[vs1_[i]];
            int d = i * 4;
            rgbaOut_[d]     = pe.r;
            rgbaOut_[d + 1] = pe.g;
            rgbaOut_[d + 2] = pe.b;
            rgbaOut_[d + 3] = 255;
        }
    } else {
        for (int i = yStart * FXW_; i < yEnd * FXW_; ++i) {
            int d = i * 4;
            rgbaOut_[d]     = remap32_[vs1_[d]];
            rgbaOut_[d + 1] = remap32_[vs1_[d + 1]];
            rgbaOut_[d + 2] = remap32_[vs1_[d + 2]];
            rgbaOut_[d + 3] = 255;
        }
    }
}

void GeissEngine::RenderFrame(float dtSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (dtSeconds > 0.001f && dtSeconds < 0.5f) {
        float instFps = 1.0f / dtSeconds;
        float mix = (intframe_ <= 32) ? 0.0f : 0.95f;
        fps_ = fps_ * mix + (1.0f - mix) * instFps;
    }

    RenderFX();
    ProcessMap();
    GetWaveData();
    RenderDots();
    RenderWave();

    std::swap(vs1_, vs2_);

    if (!bLocked_) {
        if (y_map_pos_ == -1) PickRandomMode();
        GenerateChunkOfNewMap(false, nullptr);
    }

    ConvertToRgba();
    CompositeAndStampText();
}

// --- Interactive controls ---

void GeissEngine::NextMap() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (y_map_pos_ == -1) PickRandomMode();
    g_rush_map_ = true;
    do {
        GenerateChunkOfNewMap(false, nullptr);
    } while (y_map_pos_ != -1);
}

void GeissEngine::SelectMap(int modeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    new_mode_ = std::clamp(modeId, 1, NUM_MODES);
    y_map_pos_ = -1;
    g_rush_map_ = true;
    do {
        GenerateChunkOfNewMap(false, nullptr);
    } while (y_map_pos_ != -1);
}

void GeissEngine::RandomizePalette() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool prevLock = bPalLocked_;
    bPalLocked_ = false;
    RandomPalette(false);
    bPalLocked_ = prevLock;
}

void GeissEngine::ToggleLockMode() {
    std::lock_guard<std::mutex> lock(mutex_);
    bLocked_ = !bLocked_;
}

void GeissEngine::ToggleLockPalette() {
    std::lock_guard<std::mutex> lock(mutex_);
    bPalLocked_ = !bPalLocked_;
}

void GeissEngine::CycleWaveform() {
    std::lock_guard<std::mutex> lock(mutex_);
    waveform_ = (waveform_ + 1) % (NUM_WAVES + 1);
}

void GeissEngine::SetWaveform(int wf) {
    std::lock_guard<std::mutex> lock(mutex_);
    waveform_ = std::clamp(wf, 0, NUM_WAVES);
}

void GeissEngine::ToggleSpectrumMode() {
    std::lock_guard<std::mutex> lock(mutex_);
    visMode_ = (visMode_ == VIS_SPECTRUM) ? VIS_WAVE : VIS_SPECTRUM;
}

void GeissEngine::SetSpectrumMode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    visMode_ = enabled ? VIS_SPECTRUM : VIS_WAVE;
}

void GeissEngine::ToggleEffect(int effectIdx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (effectIdx < 0 || effectIdx >= NUM_EFFECTS) return;
    effect_[effectIdx] = (effect_[effectIdx] > 0) ? -1 : 1;
}

void GeissEngine::SetEffect(int effectIdx, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (effectIdx < 0 || effectIdx >= NUM_EFFECTS) return;
    effect_[effectIdx] = enabled ? 1 : -1;
}

bool GeissEngine::IsEffectActive(int effectIdx) const {
    if (effectIdx < 0 || effectIdx >= NUM_EFFECTS) return false;
    return effect_[effectIdx] > 0;
}

void GeissEngine::ToggleSlideShift() {
    std::lock_guard<std::mutex> lock(mutex_);
    bSlideShift_ = !bSlideShift_;
    if (!bSlideShift_) slider1_ = 0;
}

void GeissEngine::AdjustVolumeScale(int delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    SetVolumePos(volpos_ + delta);
}

void GeissEngine::SetVolumePos(int pos) {
    volpos_ = std::clamp(pos, 0, 20);
    volscale_ = powf(1.25f, static_cast<float>(volpos_ - 10));
}

void GeissEngine::RefreshEdges() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t bpp = (config_.colorDepthBits == 8) ? 1u : 4u;
    int clearLines = std::min(FX_YCUT_HIDE_ + 4, FXH_ / 4);
    std::memset(vs1_.data(), 0, static_cast<size_t>(clearLines * FXW_) * bpp);
    std::memset(&vs1_[(FXH_ - clearLines) * FXW_ * bpp], 0, static_cast<size_t>(clearLines * FXW_) * bpp);
    std::memset(vs2_.data(), 0, static_cast<size_t>(clearLines * FXW_) * bpp);
    std::memset(&vs2_[(FXH_ - clearLines) * FXW_ * bpp], 0, static_cast<size_t>(clearLines * FXW_) * bpp);
}

void GeissEngine::TriggerTouchBurst(float normX, float normY, float strength) {
    std::lock_guard<std::mutex> lock(mutex_);
    float resScale = std::clamp(std::min(FXW_, FXH_) / 480.0f, 1.0f, 3.5f);
    int maxR = static_cast<int>(10.0f * resScale);
    int margin = maxR + 2;
    int cx = std::clamp(static_cast<int>(normX * FXW_), margin, std::max(margin, FXW_ - 1 - margin));
    int cy = std::clamp(static_cast<int>(normY * FXH_), FX_YCUT_HIDE_ + margin, std::max(FX_YCUT_HIDE_ + margin, FXH_ - FX_YCUT_HIDE_ - 1 - margin));

    float rad = 10.0f * std::clamp(strength, 0.4f, 1.5f) * resScale;

    float cr = 0.6f + 0.4f * sinf(floatframe_ * 0.07f);
    float cg = 0.6f + 0.4f * sinf(floatframe_ * 0.09f + 2.1f);
    float cb = 0.6f + 0.4f * sinf(floatframe_ * 0.11f + 4.2f);

    for (int y = -maxR; y <= maxR; ++y) {
        int py = cy + y;
        if (py <= FX_YCUT_ || py >= FXH_ - 1 - FX_YCUT_) continue;
        for (int x = -maxR; x <= maxR; ++x) {
            int px = cx + x;
            if (px < 0 || px >= FXW_) continue;
            float d = sqrtf(static_cast<float>(x * x + y * y));
            int val = static_cast<int>((rad - d) * (24.0f / resScale));
            if (val > 0) {
                if (config_.colorDepthBits == 8) {
                    int idx = py * FXW_ + px;
                    vs1_[idx] = static_cast<uint8_t>(std::min(255, vs1_[idx] + val));
                } else {
                    int idx = (py * FXW_ + px) * 4;
                    vs1_[idx]     = static_cast<uint8_t>(std::min(255, vs1_[idx]     + static_cast<int>(val * cr)));
                    vs1_[idx + 1] = static_cast<uint8_t>(std::min(255, vs1_[idx + 1] + static_cast<int>(val * cg)));
                    vs1_[idx + 2] = static_cast<uint8_t>(std::min(255, vs1_[idx + 2] + static_cast<int>(val * cb)));
                }
            }
        }
    }
}

void GeissEngine::SetLauncherOffset(float xOffset) {
    std::lock_guard<std::mutex> lock(mutex_);
    launcherOffset_ = std::clamp(xOffset, -0.5f, 0.5f);
}

void GeissEngine::SetModeRating(int modeId, int stars) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (modeId < 1 || modeId > NUM_MODES) return;
    modeprefs_[modeId] = std::clamp(stars, 0, 5);
    modeprefs_total_ = 0;
    for (int i = 1; i <= NUM_MODES; ++i) {
        modeprefs_total_ += modeprefs_[i];
    }
}

int GeissEngine::GetModeRating(int modeId) const {
    if (modeId < 1 || modeId > NUM_MODES) return 3;
    return modeprefs_[modeId];
}

void GeissEngine::SetAllModeRatings(const int ratings[NUM_MODES + 1]) {
    std::lock_guard<std::mutex> lock(mutex_);
    modeprefs_total_ = 0;
    for (int i = 1; i <= NUM_MODES; ++i) {
        modeprefs_[i] = std::clamp(ratings[i], 0, 5);
        modeprefs_total_ += modeprefs_[i];
    }
}

PresetData GeissEngine::ExportCurrentPreset(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    PresetData p;
    p.valid = true;
    p.name = name.empty() ? GetModeName(mode_) : name;
    p.x_center = gXC_ / static_cast<float>(std::max(1, FXW_));
    p.y_center = gYC_ / static_cast<float>(std::max(1, FXH_));
    p.mode = mode_;
    p.waveform = waveform_;
    p.damping = damping_;
    p.magic = old_weightsum_;
    p.shift = bSlideShift_;
    p.gamma = config_.gamma;
    p.volpos = volpos_;
    p.t1 = old_turn1_;
    p.t2 = old_turn2_;
    p.s1 = old_scale1_;
    p.s2 = old_scale2_;
    p.f1 = old_f1_;
    p.f2 = old_f2_;
    p.f3 = old_f3_;
    p.f4 = old_f4_;
    for (int i = 0; i < NUM_EFFECTS; ++i) {
        p.effects[i] = effect_[i];
    }
    p.spectrum = (visMode_ == VIS_SPECTRUM);
    p.palette = currentPalette_;
    return p;
}

void GeissEngine::LoadPresetData(const PresetData& preset) {
    if (!preset.valid) return;
    std::lock_guard<std::mutex> lock(mutex_);
    y_map_pos_ = -1;
    g_rush_map_ = true;
    do {
        GenerateChunkOfNewMap(true, &preset);
    } while (y_map_pos_ != -1);
}

void GeissEngine::SetSongTitleText(const std::string& title, bool isCustomMessage) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (title != currentSongTitle_ || isCustomMessage) {
        currentSongTitle_ = title;
        lastMessageWasCustom_ = isCustomMessage;
        g_title_R_ = static_cast<uint8_t>(128 + RandInt(99));
        g_title_G_ = static_cast<uint8_t>(128 + RandInt(99));
        g_title_B_ = static_cast<uint8_t>(128 + RandInt(99));
    }
}

void GeissEngine::SetOverlayTextBitmap(const uint8_t* rgba, int w, int h, int durationFrames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!rgba || w <= 0 || h <= 0) {
        overlayRgba_.clear();
        overlayW_ = 0;
        overlayH_ = 0;
        return;
    }
    overlayW_ = std::min(w, FXW_);
    overlayH_ = std::min(h, FXH_ - FX_YCUT_HIDE_ * 2);
    overlayRgba_.assign(rgba, rgba + static_cast<size_t>(w * h * 4));
    // Re-pack if clipped
    if (overlayW_ != w || overlayH_ != h) {
        std::vector<uint8_t> clipped(static_cast<size_t>(overlayW_ * overlayH_ * 4), 0);
        for (int r = 0; r < overlayH_; ++r) {
            std::memcpy(&clipped[r * overlayW_ * 4], &rgba[r * w * 4], static_cast<size_t>(overlayW_ * 4));
        }
        overlayRgba_.swap(clipped);
    }

    int xRange = std::max(1, FXW_ - overlayW_);
    int yRange = std::max(1, FXH_ - FX_YCUT_HIDE_ * 2 - overlayH_);
    overlayX_ = (RandInt(xRange / 4 + 1) + RandInt(xRange / 4 + 1) +
                 RandInt(xRange / 4 + 1) + RandInt(xRange / 4 + 1));
    overlayX_ = std::clamp(overlayX_, 0, std::max(0, FXW_ - overlayW_));
    overlayY_ = FX_YCUT_HIDE_ +
                (RandInt(yRange / 4 + 1) + RandInt(yRange / 4 + 1) +
                 RandInt(yRange / 4 + 1) + RandInt(yRange / 4 + 1));
    overlayY_ = std::clamp(overlayY_, FX_YCUT_HIDE_, std::max(FX_YCUT_HIDE_, FXH_ - FX_YCUT_HIDE_ - overlayH_));

    int scaledFrames = static_cast<int>(durationFrames * std::max(10.0f, fps_at_last_mode_switch_) / 30.0f);
    g_hit_ = std::max(4, scaledFrames);
}

void GeissEngine::TriggerSongTitlePopup() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!overlayRgba_.empty()) {
        int xRange = std::max(1, FXW_ - overlayW_);
        int yRange = std::max(1, FXH_ - FX_YCUT_HIDE_ * 2 - overlayH_);
        overlayX_ = std::clamp(RandInt(xRange), 0, std::max(0, FXW_ - overlayW_));
        overlayY_ = std::clamp(FX_YCUT_HIDE_ + RandInt(yRange), FX_YCUT_HIDE_,
                               std::max(FX_YCUT_HIDE_, FXH_ - FX_YCUT_HIDE_ - overlayH_));
        g_hit_ = std::max(4, static_cast<int>(config_.songTooltipFrames * fps_at_last_mode_switch_ / 30.0f));
        g_title_R_ = static_cast<uint8_t>(128 + RandInt(99));
        g_title_G_ = static_cast<uint8_t>(128 + RandInt(99));
        g_title_B_ = static_cast<uint8_t>(128 + RandInt(99));
    }
}

} // namespace geiss
