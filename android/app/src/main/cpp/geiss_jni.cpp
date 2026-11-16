// Geiss Android Live Wallpaper - JNI Bridge & OpenGL ES 2.0 Quad Renderer
#include <jni.h>
#include <android/log.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "geiss_engine.h"

#define LOG_TAG "GeissNative"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr const char* kVertexShader = R"GLSL(
attribute vec2 aPos;
attribute vec2 aTex;
varying vec2 vTex;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTex = aTex;
}
)GLSL";

constexpr const char* kFragmentShader = R"GLSL(
precision mediump float;
varying vec2 vTex;
uniform sampler2D uTex;
void main() {
    gl_FragColor = texture2D(uTex, vTex);
}
)GLSL";

GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        LOGE("Shader compile error: %s", buf);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

class GlRenderContext {
public:
    ~GlRenderContext() {
        Release();
    }

    void OnSurfaceCreated() {
        Release();
        GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return;
        }
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint linked = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (!linked) {
            char buf[512];
            glGetProgramInfoLog(program_, sizeof(buf), nullptr, buf);
            LOGE("Program link error: %s", buf);
            glDeleteProgram(program_);
            program_ = 0;
            return;
        }

        aPosLoc_ = glGetAttribLocation(program_, "aPos");
        aTexLoc_ = glGetAttribLocation(program_, "aTex");
        uTexLoc_ = glGetUniformLocation(program_, "uTex");

        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        texW_ = 0;
        texH_ = 0;
        lastLinear_ = true;
    }

    void OnSurfaceChanged(int w, int h) {
        surfaceW_ = std::max(1, w);
        surfaceH_ = std::max(1, h);
        glViewport(0, 0, surfaceW_, surfaceH_);
    }

    void Draw(geiss::GeissEngine* engine, float dtSeconds, bool linearFilter, float parallaxAmount) {
        if (!engine || !program_ || !texture_) return;

        engine->RenderFrame(dtSeconds);

        const int w = engine->GetWidth();
        const int h = engine->GetHeight();
        const int yCut = engine->GetYCUTHide();
        const uint8_t* rgba = engine->GetRgbaBuffer();
        if (!rgba || w <= 0 || h <= 0) return;

        glBindTexture(GL_TEXTURE_2D, texture_);

        if (linearFilter != lastLinear_) {
            GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            lastLinear_ = linearFilter;
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (w != texW_ || h != texH_) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            texW_ = w;
            texH_ = h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        }

        // Crop minimal outer safety margin; preserve intentional letterbox bars if verticalSizePct < 100
        int safeBorder = std::min(yCut, 6);
        float vMin = static_cast<float>(safeBorder) / static_cast<float>(h);
        float vMax = 1.0f - vMin;

        float pAmt = std::clamp(parallaxAmount, 0.0f, 0.25f);
        float uSpan = 1.0f - pAmt;
        float offset = engine->GetLauncherOffset(); // -0.5..0.5
        float uCenter = std::clamp(0.5f + offset * pAmt, uSpan * 0.5f, 1.0f - uSpan * 0.5f);
        float uMin = uCenter - uSpan * 0.5f;
        float uMax = uCenter + uSpan * 0.5f;

        // Top of RGBA buffer (row 0) maps to top of GL NDC (+1.0)
        const GLfloat verts[] = {
            // x,     y,     u,    v
            -1.0f, -1.0f, uMin, vMax,
             1.0f, -1.0f, uMax, vMax,
            -1.0f,  1.0f, uMin, vMin,
             1.0f,  1.0f, uMax, vMin,
        };

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glUniform1i(uTexLoc_, 0);

        glEnableVertexAttribArray(aPosLoc_);
        glVertexAttribPointer(aPosLoc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts);
        glEnableVertexAttribArray(aTexLoc_);
        glVertexAttribPointer(aTexLoc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts + 2);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(aPosLoc_);
        glDisableVertexAttribArray(aTexLoc_);
    }

    void Release() {
        if (texture_) {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
        }
        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }
        texW_ = 0;
        texH_ = 0;
    }

private:
    GLuint program_ = 0;
    GLuint texture_ = 0;
    GLint aPosLoc_ = -1;
    GLint aTexLoc_ = -1;
    GLint uTexLoc_ = -1;
    int texW_ = 0;
    int texH_ = 0;
    int surfaceW_ = 1;
    int surfaceH_ = 1;
    bool lastLinear_ = true;
};

geiss::GeissConfig BuildConfig(
        jint colorDepthBits,
        jint verticalSizePct,
        jint framesTilAutoSwitch,
        jint gamma,
        jint solarPalFreq,
        jint coarsePalFreq,
        jint volpos,
        jboolean useBeatDetection,
        jboolean syncColorToSound,
        jboolean suppressAllMsg,
        jboolean disableSongTitlePopups,
        jint randomSongTitleFreq,
        jint songTooltipFrames,
        jint slideShiftFreq,
        jint slideShiftMinFrames,
        jboolean showFps,
        jboolean autoDemoWhenSilent) {
    geiss::GeissConfig cfg;
    cfg.colorDepthBits = colorDepthBits;
    cfg.verticalSizePct = verticalSizePct;
    cfg.framesTilAutoSwitch = framesTilAutoSwitch;
    cfg.gamma = gamma;
    cfg.solarPalFreq = solarPalFreq;
    cfg.coarsePalFreq = coarsePalFreq;
    cfg.volpos = volpos;
    cfg.useBeatDetection = useBeatDetection == JNI_TRUE;
    cfg.syncColorToSound = syncColorToSound == JNI_TRUE;
    cfg.suppressAllMsg = suppressAllMsg == JNI_TRUE;
    cfg.disableSongTitlePopups = disableSongTitlePopups == JNI_TRUE;
    cfg.randomSongTitleFreq = randomSongTitleFreq;
    cfg.songTooltipFrames = songTooltipFrames;
    cfg.slideShiftFreq = slideShiftFreq;
    cfg.slideShiftMinFrames = slideShiftMinFrames;
    cfg.showFps = showFps == JNI_TRUE;
    cfg.autoDemoWhenSilent = autoDemoWhenSilent == JNI_TRUE;
    return cfg;
}

inline geiss::GeissEngine* AsEngine(jlong handle) {
    return reinterpret_cast<geiss::GeissEngine*>(handle);
}

inline GlRenderContext* AsGl(jlong handle) {
    return reinterpret_cast<GlRenderContext*>(handle);
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeCreate(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new geiss::GeissEngine());
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete AsEngine(handle);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeInit(
        JNIEnv*, jclass, jlong handle, jint width, jint height,
        jint colorDepthBits, jint verticalSizePct, jint framesTilAutoSwitch,
        jint gamma, jint solarPalFreq, jint coarsePalFreq, jint volpos,
        jboolean useBeatDetection, jboolean syncColorToSound,
        jboolean suppressAllMsg, jboolean disableSongTitlePopups,
        jint randomSongTitleFreq, jint songTooltipFrames,
        jint slideShiftFreq, jint slideShiftMinFrames,
        jboolean showFps, jboolean autoDemoWhenSilent) {
    auto* eng = AsEngine(handle);
    if (!eng) return;
    eng->Init(width, height, BuildConfig(
        colorDepthBits, verticalSizePct, framesTilAutoSwitch, gamma,
        solarPalFreq, coarsePalFreq, volpos, useBeatDetection, syncColorToSound,
        suppressAllMsg, disableSongTitlePopups, randomSongTitleFreq, songTooltipFrames,
        slideShiftFreq, slideShiftMinFrames, showFps, autoDemoWhenSilent));
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeResize(JNIEnv*, jclass, jlong handle, jint width, jint height) {
    if (auto* eng = AsEngine(handle)) eng->Resize(width, height);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeUpdateConfig(
        JNIEnv*, jclass, jlong handle,
        jint colorDepthBits, jint verticalSizePct, jint framesTilAutoSwitch,
        jint gamma, jint solarPalFreq, jint coarsePalFreq, jint volpos,
        jboolean useBeatDetection, jboolean syncColorToSound,
        jboolean suppressAllMsg, jboolean disableSongTitlePopups,
        jint randomSongTitleFreq, jint songTooltipFrames,
        jint slideShiftFreq, jint slideShiftMinFrames,
        jboolean showFps, jboolean autoDemoWhenSilent) {
    auto* eng = AsEngine(handle);
    if (!eng) return;
    eng->UpdateConfig(BuildConfig(
        colorDepthBits, verticalSizePct, framesTilAutoSwitch, gamma,
        solarPalFreq, coarsePalFreq, volpos, useBeatDetection, syncColorToSound,
        suppressAllMsg, disableSongTitlePopups, randomSongTitleFreq, songTooltipFrames,
        slideShiftFreq, slideShiftMinFrames, showFps, autoDemoWhenSilent));
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeFeedWaveAndSpectrum8(
        JNIEnv* env, jclass, jlong handle,
        jbyteArray waveL, jbyteArray waveR, jint waveLen,
        jbyteArray specL, jbyteArray specR, jint specLen,
        jboolean active) {
    auto* eng = AsEngine(handle);
    if (!eng) return;
    jbyte* wL = waveL ? env->GetByteArrayElements(waveL, nullptr) : nullptr;
    jbyte* wR = waveR ? env->GetByteArrayElements(waveR, nullptr) : nullptr;
    jbyte* sL = specL ? env->GetByteArrayElements(specL, nullptr) : nullptr;
    jbyte* sR = specR ? env->GetByteArrayElements(specR, nullptr) : nullptr;

    eng->FeedWaveAndSpectrum8(
        reinterpret_cast<const uint8_t*>(wL),
        reinterpret_cast<const uint8_t*>(wR), waveLen,
        reinterpret_cast<const uint8_t*>(sL),
        reinterpret_cast<const uint8_t*>(sR), specLen,
        active == JNI_TRUE);

    if (wL) env->ReleaseByteArrayElements(waveL, wL, JNI_ABORT);
    if (wR) env->ReleaseByteArrayElements(waveR, wR, JNI_ABORT);
    if (sL) env->ReleaseByteArrayElements(specL, sL, JNI_ABORT);
    if (sR) env->ReleaseByteArrayElements(specR, sR, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeFeedPcm16Stereo(
        JNIEnv* env, jclass, jlong handle,
        jshortArray pcm, jint numFrames, jboolean active) {
    auto* eng = AsEngine(handle);
    if (!eng || !pcm) return;
    jshort* ptr = env->GetShortArrayElements(pcm, nullptr);
    if (ptr) {
        eng->FeedPcm16Stereo(reinterpret_cast<const int16_t*>(ptr), numFrames, active == JNI_TRUE);
        env->ReleaseShortArrayElements(pcm, ptr, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeNextMap(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->NextMap();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSelectMap(JNIEnv*, jclass, jlong handle, jint modeId) {
    if (auto* eng = AsEngine(handle)) eng->SelectMap(modeId);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeRandomizePalette(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->RandomizePalette();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeToggleLockMode(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->ToggleLockMode();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeToggleLockPalette(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->ToggleLockPalette();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeCycleWaveform(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->CycleWaveform();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetWaveform(JNIEnv*, jclass, jlong handle, jint wf) {
    if (auto* eng = AsEngine(handle)) eng->SetWaveform(wf);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeToggleSpectrumMode(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->ToggleSpectrumMode();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetSpectrumMode(JNIEnv*, jclass, jlong handle, jboolean enabled) {
    if (auto* eng = AsEngine(handle)) eng->SetSpectrumMode(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeToggleEffect(JNIEnv*, jclass, jlong handle, jint effectIdx) {
    if (auto* eng = AsEngine(handle)) eng->ToggleEffect(effectIdx);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetEffect(JNIEnv*, jclass, jlong handle, jint effectIdx, jboolean enabled) {
    if (auto* eng = AsEngine(handle)) eng->SetEffect(effectIdx, enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeToggleSlideShift(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->ToggleSlideShift();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeAdjustVolumeScale(JNIEnv*, jclass, jlong handle, jint delta) {
    if (auto* eng = AsEngine(handle)) eng->AdjustVolumeScale(delta);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetVolumePos(JNIEnv*, jclass, jlong handle, jint pos) {
    if (auto* eng = AsEngine(handle)) eng->SetVolumePos(pos);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeRefreshEdges(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->RefreshEdges();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeTriggerTouchBurst(JNIEnv*, jclass, jlong handle, jfloat normX, jfloat normY, jfloat strength) {
    if (auto* eng = AsEngine(handle)) eng->TriggerTouchBurst(normX, normY, strength);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetLauncherOffset(JNIEnv*, jclass, jlong handle, jfloat offset) {
    if (auto* eng = AsEngine(handle)) eng->SetLauncherOffset(offset);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetModeRating(JNIEnv*, jclass, jlong handle, jint modeId, jint stars) {
    if (auto* eng = AsEngine(handle)) eng->SetModeRating(modeId, stars);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetAllModeRatings(JNIEnv* env, jclass, jlong handle, jintArray ratings) {
    auto* eng = AsEngine(handle);
    if (!eng || !ratings) return;
    jsize len = env->GetArrayLength(ratings);
    if (len < geiss::NUM_MODES + 1) return;
    jint buf[geiss::NUM_MODES + 1] = {};
    env->GetIntArrayRegion(ratings, 0, geiss::NUM_MODES + 1, buf);
    int converted[geiss::NUM_MODES + 1];
    for (int i = 0; i <= geiss::NUM_MODES; ++i) converted[i] = buf[i];
    eng->SetAllModeRatings(converted);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeExportPreset(
        JNIEnv* env, jclass, jlong handle, jstring name,
        jfloatArray outFloats, jintArray outInts, jbooleanArray outBools) {
    auto* eng = AsEngine(handle);
    if (!eng || !outFloats || !outInts || !outBools) return;
    const char* nameChars = name ? env->GetStringUTFChars(name, nullptr) : "";
    geiss::PresetData p = eng->ExportCurrentPreset(nameChars ? nameChars : "");
    if (name && nameChars) env->ReleaseStringUTFChars(name, nameChars);

    jfloat floats[11] = {
        p.x_center, p.y_center, p.damping,
        p.t1, p.t2, p.s1, p.s2,
        p.f1, p.f2, p.f3, p.f4
    };
    jint ints[19] = {
        p.mode, p.waveform, p.magic, p.gamma, p.volpos,
        p.palette.lo_band, p.palette.hi_band, p.palette.iFXPaletteNum,
        p.palette.c1, p.palette.c2, p.palette.c3,
        p.effects[0], p.effects[1], p.effects[2], p.effects[3],
        p.effects[4], p.effects[5], p.effects[6], p.effects[7]
    };
    jboolean bools[4] = {
        static_cast<jboolean>(p.valid ? JNI_TRUE : JNI_FALSE),
        static_cast<jboolean>(p.shift ? JNI_TRUE : JNI_FALSE),
        static_cast<jboolean>(p.spectrum ? JNI_TRUE : JNI_FALSE),
        static_cast<jboolean>(p.palette.bFXPalette ? JNI_TRUE : JNI_FALSE)
    };
    env->SetFloatArrayRegion(outFloats, 0, 11, floats);
    env->SetIntArrayRegion(outInts, 0, 19, ints);
    env->SetBooleanArrayRegion(outBools, 0, 4, bools);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeLoadPreset(
        JNIEnv* env, jclass, jlong handle, jstring name,
        jfloatArray inFloats, jintArray inInts, jbooleanArray inBools) {
    auto* eng = AsEngine(handle);
    if (!eng || !inFloats || !inInts || !inBools) return;
    if (env->GetArrayLength(inFloats) < 11 ||
        env->GetArrayLength(inInts) < 19 ||
        env->GetArrayLength(inBools) < 4) return;

    jfloat floats[11];
    jint ints[19];
    jboolean bools[4];
    env->GetFloatArrayRegion(inFloats, 0, 11, floats);
    env->GetIntArrayRegion(inInts, 0, 19, ints);
    env->GetBooleanArrayRegion(inBools, 0, 4, bools);

    geiss::PresetData p;
    if (name) {
        const char* chars = env->GetStringUTFChars(name, nullptr);
        if (chars) {
            p.name = chars;
            env->ReleaseStringUTFChars(name, chars);
        }
    }
    p.valid = bools[0] == JNI_TRUE;
    p.shift = bools[1] == JNI_TRUE;
    p.spectrum = bools[2] == JNI_TRUE;
    p.palette.bFXPalette = bools[3] == JNI_TRUE;

    p.x_center = floats[0];
    p.y_center = floats[1];
    p.damping = floats[2];
    p.t1 = floats[3];
    p.t2 = floats[4];
    p.s1 = floats[5];
    p.s2 = floats[6];
    p.f1 = floats[7];
    p.f2 = floats[8];
    p.f3 = floats[9];
    p.f4 = floats[10];

    p.mode = ints[0];
    p.waveform = ints[1];
    p.magic = ints[2];
    p.gamma = ints[3];
    p.volpos = ints[4];
    p.palette.lo_band = ints[5];
    p.palette.hi_band = ints[6];
    p.palette.iFXPaletteNum = ints[7];
    p.palette.c1 = ints[8];
    p.palette.c2 = ints[9];
    p.palette.c3 = ints[10];
    for (int i = 0; i < geiss::NUM_EFFECTS; ++i) {
        p.effects[i] = ints[11 + i];
    }
    eng->LoadPresetData(p);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetSongTitleText(
        JNIEnv* env, jclass, jlong handle, jstring title, jboolean isCustom) {
    auto* eng = AsEngine(handle);
    if (!eng) return;
    std::string str;
    if (title) {
        const char* chars = env->GetStringUTFChars(title, nullptr);
        if (chars) {
            str = chars;
            env->ReleaseStringUTFChars(title, chars);
        }
    }
    eng->SetSongTitleText(str, isCustom == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeSetOverlayTextBitmap(
        JNIEnv* env, jclass, jlong handle, jbyteArray rgba, jint w, jint h, jint durationFrames) {
    auto* eng = AsEngine(handle);
    if (!eng) return;
    if (!rgba) {
        eng->SetOverlayTextBitmap(nullptr, 0, 0, 0);
        return;
    }
    jbyte* ptr = env->GetByteArrayElements(rgba, nullptr);
    if (ptr) {
        eng->SetOverlayTextBitmap(reinterpret_cast<const uint8_t*>(ptr), w, h, durationFrames);
        env->ReleaseByteArrayElements(rgba, ptr, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeTriggerSongTitlePopup(JNIEnv*, jclass, jlong handle) {
    if (auto* eng = AsEngine(handle)) eng->TriggerSongTitlePopup();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeGetStatus(
        JNIEnv* env, jclass, jlong handle, jintArray outInts, jfloatArray outFloats) {
    auto* eng = AsEngine(handle);
    if (!eng || !outInts || !outFloats) return;
    if (env->GetArrayLength(outInts) < 17 || env->GetArrayLength(outFloats) < 2) return;

    jint ints[17] = {};
    for (int i = 0; i < geiss::NUM_EFFECTS; ++i) {
        ints[i] = eng->IsEffectActive(i) ? 1 : 0;
    }
    ints[8]  = eng->GetCurrentMode();
    ints[9]  = eng->GetCurrentWaveform();
    ints[10] = eng->IsSpectrumMode() ? 1 : 0;
    ints[11] = eng->IsModeLocked() ? 1 : 0;
    ints[12] = eng->IsPaletteLocked() ? 1 : 0;
    ints[13] = eng->IsSlideShift() ? 1 : 0;
    ints[14] = eng->GetVolumePos();
    ints[15] = eng->IsSoundActive() ? 1 : 0;
    ints[16] = eng->GetColorDepthBits();

    jfloat floats[2] = {
        eng->GetCurrentFps(),
        eng->GetCurrentVolume()
    };

    env->SetIntArrayRegion(outInts, 0, 17, ints);
    env->SetFloatArrayRegion(outFloats, 0, 2, floats);
}

JNIEXPORT jstring JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeGetModeName(JNIEnv* env, jclass, jint modeId) {
    return env->NewStringUTF(geiss::GeissEngine::GetModeName(modeId));
}

// OpenGL ES 2.0 Renderer JNI methods
JNIEXPORT jlong JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeGlCreate(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new GlRenderContext());
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeGlDestroy(JNIEnv*, jclass, jlong glHandle) {
    delete AsGl(glHandle);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeGlOnSurfaceCreated(JNIEnv*, jclass, jlong glHandle) {
    if (auto* gl = AsGl(glHandle)) gl->OnSurfaceCreated();
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeGlOnSurfaceChanged(JNIEnv*, jclass, jlong glHandle, jint w, jint h) {
    if (auto* gl = AsGl(glHandle)) gl->OnSurfaceChanged(w, h);
}

JNIEXPORT void JNICALL
Java_com_geisswerks_geiss_GeissNative_nativeGlDrawFrame(
        JNIEnv*, jclass, jlong glHandle, jlong engineHandle,
        jfloat dtSeconds, jboolean linearFilter, jfloat parallaxAmount) {
    auto* gl = AsGl(glHandle);
    auto* eng = AsEngine(engineHandle);
    if (gl && eng) {
        gl->Draw(eng, dtSeconds, linearFilter == JNI_TRUE, parallaxAmount);
    }
}

} // extern "C"
