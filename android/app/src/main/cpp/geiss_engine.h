// Geiss Android Live Wallpaper - Portable C++ Visualization Engine
// Derived from Geiss Screensaver & Winamp Plug-in (3-Clause BSD License)
// Original Copyright (c) 1998-2022 Ryan Geiss (@geissomatik)

#ifndef GEISS_ANDROID_ENGINE_H
#define GEISS_ANDROID_ENGINE_H

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace geiss {

constexpr int NUM_MODES = 25;
constexpr int NUM_EFFECTS = 8; // CHASERS, BAR, DOTS, SOLAR, GRID, NUCLIDE, SHADE, SPECTRAL
constexpr int NUM_WAVES = 6;   // 0=off, 1..6 waveforms
constexpr int WAVE_5_BLEND_RANGE = 50;
constexpr int PAST_VOL_N = 30;
constexpr int NUM_PRESET_SLOTS = 100;

enum EffectType {
    EFF_CHASERS = 0,
    EFF_BAR = 1,
    EFF_DOTS = 2,
    EFF_SOLAR = 3,
    EFF_GRID = 4,
    EFF_NUCLIDE = 5,
    EFF_SHADE = 6,
    EFF_SPECTRAL = 7
};

enum VisMode {
    VIS_WAVE = 0,
    VIS_SPECTRUM = 1
};

struct PaletteEntry {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct PaletteState {
    int lo_band = -1;
    int hi_band = -1;
    bool bFXPalette = false;
    int iFXPaletteNum = 0;
    int c1 = 1;
    int c2 = 2;
    int c3 = 3;
};

struct PresetData {
    bool valid = false;
    std::string name;
    float x_center = 0.5f;
    float y_center = 0.5f;
    int mode = 5;
    int waveform = 1;
    float damping = 1.0f;
    int magic = 256;
    bool shift = false;
    int gamma = 10;
    int volpos = 10;
    float t1 = 0.01f;
    float t2 = 0.01f;
    float s1 = 0.96f;
    float s2 = 0.96f;
    float f1 = 0.95f;
    float f2 = 0.001f;
    float f3 = 0.05f;
    float f4 = 0.05f;
    int effects[NUM_EFFECTS] = {-1, -1, -1, -1, -1, -1, -1, -1};
    bool spectrum = false;
    PaletteState palette;
};

struct GeissConfig {
    int colorDepthBits = 8;              // 8 (paletted) or 32 (truecolor)
    int verticalSizePct = 96;            // 30..100
    int framesTilAutoSwitch = 550;       // at 30fps equivalent
    int gamma = 10;                      // 0..100
    int solarPalFreq = 1;                // 0..5
    int coarsePalFreq = 1;               // 0..10
    int volpos = 10;                     // 0..20
    bool useBeatDetection = true;
    bool syncColorToSound = true;
    bool suppressAllMsg = false;
    bool disableSongTitlePopups = false;
    int randomSongTitleFreq = 3;         // 0..10
    int songTooltipFrames = 45;          // frames to show before dissolving into VS
    int slideShiftFreq = 25;             // 0..100 % chance per mode
    int slideShiftMinFrames = 6;
    bool showFps = false;
    bool autoDemoWhenSilent = false;     // optional gentle fallback wave if mic/mix silent
};

struct ModeInfo {
    int effect_freq[NUM_EFFECTS] = {0};
    int solar_max = 60;
    float center_dwindle = 1.0f;
    int min_effects = 1;
    int max_effects = 3;
};

struct MapCell {
    uint8_t w0, w1, w2, w3;
    int32_t lookat;
};

class GeissEngine {
public:
    GeissEngine();
    ~GeissEngine();

    void Init(int width, int height, const GeissConfig& cfg);
    void Resize(int width, int height);
    void UpdateConfig(const GeissConfig& cfg);

    // Feed audio: either raw 8-bit unsigned waveform (like Visualizer) or 16-bit stereo PCM
    void FeedWaveAndSpectrum8(const uint8_t* waveL, const uint8_t* waveR, int waveLen,
                              const uint8_t* specL, const uint8_t* specR, int specLen,
                              bool active);
    void FeedPcm16Stereo(const int16_t* interleavedLR, int numFrames, bool active);

    // Step simulation & render frame into internal RGBA8888 buffer (width * height * 4 bytes)
    void RenderFrame(float dtSeconds);
    const uint8_t* GetRgbaBuffer() const { return rgbaOut_.data(); }
    int GetWidth() const { return FXW_; }
    int GetHeight() const { return FXH_; }
    int GetYCUTHide() const { return FX_YCUT_HIDE_; }

    // Interactive Controls (all classic Geiss hotkeys/features)
    void NextMap();                                  // 'n'
    void SelectMap(int modeId);                      // '01'..'25'
    void RandomizePalette();                         // 'p'
    void ToggleLockMode();                           // 'l'
    void ToggleLockPalette();                        // 't'
    void CycleWaveform();                            // 'w'
    void SetWaveform(int wf);                        // 0..6
    void ToggleSpectrumMode();                       // wave <-> spectrum
    void SetSpectrumMode(bool enabled);
    void ToggleEffect(int effectIdx);                // q/u/d/Y/g/A/e
    void SetEffect(int effectIdx, bool enabled);
    void ToggleSlideShift();                         // 'i'
    void AdjustVolumeScale(int delta);               // 'j'/'k'
    void SetVolumePos(int pos);                      // 0..20
    void RefreshEdges();                             // 'r'
    void TriggerTouchBurst(float normX, float normY, float strength = 1.0f);
    void SetLauncherOffset(float xOffset);           // 0..1 home screen parallax

    // Mode ratings (0..5 stars)
    void SetModeRating(int modeId, int stars);
    int GetModeRating(int modeId) const;
    void SetAllModeRatings(const int ratings[NUM_MODES + 1]);

    // Presets (export/import current state)
    PresetData ExportCurrentPreset(const std::string& name = "") const;
    void LoadPresetData(const PresetData& preset);

    // Song title / Custom message overlay bitmap (RGBA8888 from Android Canvas)
    void SetSongTitleText(const std::string& title, bool isCustomMessage);
    void SetOverlayTextBitmap(const uint8_t* rgba, int w, int h, int durationFrames);
    void TriggerSongTitlePopup();

    // Status getters for UI HUD
    int GetCurrentMode() const { return mode_; }
    int GetCurrentWaveform() const { return waveform_; }
    bool IsSpectrumMode() const { return visMode_ == VIS_SPECTRUM; }
    bool IsModeLocked() const { return bLocked_; }
    bool IsPaletteLocked() const { return bPalLocked_; }
    bool IsSlideShift() const { return bSlideShift_; }
    bool IsEffectActive(int effectIdx) const;
    int GetVolumePos() const { return volpos_; }
    float GetCurrentFps() const { return fps_; }
    float GetCurrentVolume() const { return current_vol_; }
    bool IsSoundActive() const { return soundActive_ && !soundEmpty_; }
    int GetColorDepthBits() const { return config_.colorDepthBits; }
    float GetLauncherOffset() const { return launcherOffset_; }

    static const char* GetModeName(int modeId);

private:
    void AllocateBuffers();
    void InitModeInfo();
    void PickRandomMode();
    void GenerateChunkOfNewMap(bool loadPreset = false, const PresetData* preset = nullptr);
    void ProcessMap();
    void RenderFX();
    void GetWaveData();
    void RenderDots();
    void RenderWave();
    void ConvertToRgba();

    // Effects
    void ShadeBobs();
    void DiminishCenter();
    void DropSolarParticles(int n);
    void SolidLine(float frame, int channelOffset);
    void TwoChasers(float frame);
    void Nuclide();
    void OneDottyChaser(float frame);
    void Grid();

    // Palette
    void RandomPalette(bool loadPal);
    void UpdatePaletteBlend();
    float CrankPal(unsigned int curve_id, int z) const;

    // Text stamp
    void CompositeAndStampText();

    // RNG helper
    int RandInt(int maxExclusive);
    float RandFloat();

    mutable std::mutex mutex_;

    GeissConfig config_;
    int FXW_ = 480;
    int FXH_ = 800;
    int FX_YCUT_ = 16;
    int FX_YCUT_HIDE_ = 18;
    int FX_YCUT_NUM_LINES_ = 0;
    int FXW_x_FXH_ = 0;
    int BUFSIZE_ = 1024;

    std::vector<uint8_t> vs1_;
    std::vector<uint8_t> vs2_;
    std::vector<MapCell> mapCurr_;
    std::vector<MapCell> mapNext_;
    std::vector<uint8_t> rgbaOut_;

    // Audio buffers (thread-safe snapshot updated by Feed*)
    std::mutex audioMutex_;
    std::vector<uint8_t> inWaveL_;
    std::vector<uint8_t> inWaveR_;
    std::vector<uint8_t> inSpecL_;
    std::vector<uint8_t> inSpecR_;
    bool pendingAudioActive_ = false;
    uint64_t lastAudioFeedMs_ = 0;

    std::vector<int16_t> soundBuffer_;
    std::vector<float> fSoundBuffer_;
    bool soundReady_ = true;
    bool soundActive_ = false;
    bool soundEmpty_ = true;
    int framesSinceSilence_ = 0;
    int last_frame_v_ = 0;
    int last_frame_slope_ = 1;

    // Beat detection & volume tracking
    float past_vol_[PAST_VOL_N] = {0};
    int past_vol_pos_ = 0;
    float current_vol_ = 0.0f;
    float avg_vol_ = 1.0f;
    float avg_vol_wide_ = 1.0f;
    float avg_vol_narrow_ = 1.0f;
    bool bBeatMode_ = false;
    bool bBigBeat_ = false;
    float fBigBeatThreshold_ = 1.10f;
    float shiftMaxVol_ = 1.0f;
    int framesSinceShift_ = 0;
    int slider1_ = 0;

    // Simulation state
    int mode_ = 1;
    int new_mode_ = 1;
    int gXC_ = 240;
    int gYC_ = 400;
    int new_gXC_ = 240;
    int new_gYC_ = 400;
    float damping_ = 1.0f;
    float new_damping_ = 1.0f;
    float suggested_damping_ = 1.0f;
    int weightsum_ = 256;
    int weightsum_res_adjusted_ = 253;
    int old_weightsum_ = 256;

    float scale1_ = 0.96f, scale2_ = 0.96f;
    float turn1_ = 0.01f, turn2_ = 0.01f;
    float old_scale1_ = 0.96f, old_scale2_ = 0.96f;
    float old_turn1_ = 0.01f, old_turn2_ = 0.01f;
    float cos_turn1_ = 1.0f, sin_turn1_ = 0.0f;
    float cos_turn2_ = 1.0f, sin_turn2_ = 0.0f;
    float f1_ = 0.95f, f2_ = 0.001f, f3_ = 0.05f, f4_ = 0.05f;
    float old_f1_ = 0.95f, old_f2_ = 0.001f, old_f3_ = 0.05f, old_f4_ = 0.05f;

    float cx_[10] = {0}, cy_[10] = {0}, ci_[10] = {0}, cj_[10] = {0}, cr_[10] = {100}, cr_inv_[10] = {0.01f};
    int ctype_[10] = {0};

    float center_dwindle_ = 1.0f;
    int solar_max_ = 60;
    int grid_dir_ = 1;

    float floatframe_ = 0.0f;
    long intframe_ = 0;
    long frames_this_mode_ = 0;
    long chaser_offset_ = 0;
    int clearframes_ = 4;
    int frames_til_auto_switch_ = 550;
    int frames_crunching_this_mode_ = 0;
    int y_map_pos_ = -1;
    bool g_rush_map_ = false;
    bool g_bLoadPreset_ = false;
    PresetData pendingPreset_;

    int waveform_ = 1;
    int new_waveform_ = 1;
    VisMode visMode_ = VIS_WAVE;
    int effect_[NUM_EFFECTS] = {-1, -1, -1, -1, -1, -1, -1, -1};
    bool bLocked_ = false;
    bool bPalLocked_ = false;
    bool bSlideShift_ = false;

    int volpos_ = 10;
    float volscale_ = 1.0f;

    float fps_ = 30.0f;
    float fps_at_last_mode_switch_ = 30.0f;

    // Mode preferences & info
    int modeprefs_[NUM_MODES + 1];
    int modeprefs_total_ = 0;
    ModeInfo modeInfo_[NUM_MODES + 1];
    bool mode_motion_dampened_[NUM_MODES + 1] = {};
    bool rotation_dither_[NUM_MODES + 1] = {};
    bool custom_motion_vectors_[NUM_MODES + 1] = {};

    // Lookup tables & micro oscillators
    float gF_[6] = {};
    float micro_f1_[10] = {}, micro_f2_[10] = {}, micro_f3_[10] = {}, micro_f4_[10] = {};
    float micro_c1_[10] = {}, micro_c2_[10] = {}, micro_c3_[10] = {};
    float micro_rad_[4][10] = {};
    float rand_array_[2345] = {};
    int rand_array_pos_ = 0;
    float sqrt_tab_[21][21] = {};

    int chaser_x_[20] = {}, chaser_y_[20] = {};
    int chaser_ptr_ = 0;
    uint8_t chaser_r_[20] = {}, chaser_g_[20] = {}, chaser_b_[20] = {};

    // Palettes
    PaletteEntry ape_[256];
    PaletteEntry ape2_[256];
    PaletteEntry apetemp_[256];
    PaletteState currentPalette_;
    int iBlendsLeftInPal_ = 0;
    uint8_t remap32_[256] = {};

    // Launcher swipe parallax (-0.5..0.5)
    float launcherOffset_ = 0.0f;

    // Song title / Custom message overlay
    std::vector<uint8_t> overlayRgba_;
    int overlayW_ = 0;
    int overlayH_ = 0;
    int overlayX_ = 0;
    int overlayY_ = 0;
    int g_hit_ = -1;
    uint8_t g_title_R_ = 225, g_title_G_ = 225, g_title_B_ = 225;
    std::string currentSongTitle_;
    bool lastMessageWasCustom_ = false;

    uint32_t rngState_ = 0x13579BDFu;
};

} // namespace geiss

#endif // GEISS_ANDROID_ENGINE_H
