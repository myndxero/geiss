package com.geisswerks.geiss

import android.content.Context
import android.opengl.GLSurfaceView
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class GeissGlRenderer(context: Context) :
    GLSurfaceView.Renderer,
    GeissAudioManager.AudioConsumer,
    GeissController.EngineSurface {

    private val controller = GeissController.getInstance(context)
    private val lifecycleLock = Any()

    @Volatile
    private var engineHandle: Long = GeissNative.nativeCreate()

    @Volatile
    private var glHandle: Long = 0L

    @Volatile
    private var engineInitialized = false

    @Volatile
    private var surfaceW = 0

    @Volatile
    private var surfaceH = 0

    @Volatile
    private var renderW = 480

    @Volatile
    private var renderH = 800

    @Volatile
    private var snapshot: GeissConfigSnapshot = controller.prefs.getSnapshot()

    @Volatile
    private var active = false

    @Volatile
    private var latestStatus: GeissNative.EngineStatus? = null

    private var lastFrameNs = System.nanoTime()
    private var lastStatusPublishMs = 0L

    fun setActive(isActive: Boolean) {
        if (active == isActive) return
        active = isActive
        if (isActive) {
            lastFrameNs = System.nanoTime()
            controller.registerSurface(this)
            GeissAudioManager.registerConsumer(this)
        } else {
            GeissAudioManager.unregisterConsumer(this)
            controller.unregisterSurface(this)
        }
    }

    fun destroy() {
        setActive(false)
        synchronized(lifecycleLock) {
            if (glHandle != 0L) {
                GeissNative.nativeGlDestroy(glHandle)
                glHandle = 0L
            }
            if (engineHandle != 0L) {
                GeissNative.nativeDestroy(engineHandle)
                engineHandle = 0L
            }
            engineInitialized = false
        }
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        synchronized(lifecycleLock) {
            if (glHandle != 0L) {
                GeissNative.nativeGlDestroy(glHandle)
            }
            glHandle = GeissNative.nativeGlCreate()
            GeissNative.nativeGlOnSurfaceCreated(glHandle)
            lastFrameNs = System.nanoTime()
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        synchronized(lifecycleLock) {
            surfaceW = width
            surfaceH = height
            val snap = controller.prefs.getSnapshot()
            snapshot = snap
            val (rw, rh) = GeissPreferences.computeRenderSize(width, height, snap.renderResolution)
            renderW = rw
            renderH = rh

            val eng = engineHandle
            if (eng != 0L) {
                if (!engineInitialized) {
                    GeissNative.nativeInit(
                        eng, rw, rh,
                        snap.colorDepthBits,
                        snap.verticalSizePct,
                        snap.framesTilAutoSwitch,
                        snap.gamma,
                        snap.solarPalFreq,
                        snap.coarsePalFreq,
                        snap.volpos,
                        snap.useBeatDetection,
                        snap.syncColorToSound,
                        snap.suppressAllMsg,
                        snap.disableSongTitlePopups,
                        snap.randomSongTitleFreq,
                        snap.songTooltipFrames,
                        snap.slideShiftFreq,
                        snap.slideShiftMinFrames,
                        snap.showFps,
                        snap.autoDemoWhenSilent
                    )
                    GeissNative.nativeSetAllModeRatings(eng, controller.prefs.getModeRatings())
                    engineInitialized = true
                } else {
                    GeissNative.nativeResize(eng, rw, rh)
                }
            }
            if (glHandle != 0L) {
                GeissNative.nativeGlOnSurfaceChanged(glHandle, width, height)
            }
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        val now = System.nanoTime()
        val dt = ((now - lastFrameNs) / 1_000_000_000.0f).coerceIn(0.001f, 0.1f)
        lastFrameNs = now

        val snap = snapshot
        val eng: Long
        val glCtx: Long
        synchronized(lifecycleLock) {
            eng = engineHandle
            glCtx = glHandle
            if (eng == 0L || glCtx == 0L || !engineInitialized) return

            if (surfaceW > 0 && surfaceH > 0) {
                val (targetW, targetH) = GeissPreferences.computeRenderSize(
                    surfaceW,
                    surfaceH,
                    snap.renderResolution
                )
                if (targetW != renderW || targetH != renderH) {
                    renderW = targetW
                    renderH = targetH
                    GeissNative.nativeResize(eng, targetW, targetH)
                }
            }
            GeissNative.nativeGlDrawFrame(
                glCtx,
                eng,
                dt,
                snap.linearFilter,
                snap.parallaxAmount
            )
        }

        val nowMs = System.currentTimeMillis()
        if (nowMs - lastStatusPublishMs >= 150L) {
            lastStatusPublishMs = nowMs
            val st = GeissNative.readStatus(eng)
            latestStatus = st
            controller.notifyStatusSample(st)
        }
    }

    fun releaseGlResourcesOnly() {
        synchronized(lifecycleLock) {
            if (glHandle != 0L) {
                GeissNative.nativeGlDestroy(glHandle)
                glHandle = 0L
            }
        }
    }

    fun setLauncherOffset(offset: Float) {
        val eng = engineHandle
        if (eng != 0L && engineInitialized) {
            GeissNative.nativeSetLauncherOffset(eng, offset)
        }
    }

    fun triggerTouchBurst(normX: Float, normY: Float, strength: Float = 1.0f) {
        val eng = engineHandle
        if (eng != 0L && engineInitialized && snapshot.touchBurstEnabled) {
            GeissNative.nativeTriggerTouchBurst(eng, normX, normY, strength)
        }
    }

    override fun onWaveAndSpectrum8(
        waveL: ByteArray?,
        waveR: ByteArray?,
        waveLen: Int,
        specL: ByteArray?,
        specR: ByteArray?,
        specLen: Int,
        active: Boolean
    ) {
        val eng = engineHandle
        if (eng != 0L && engineInitialized) {
            GeissNative.nativeFeedWaveAndSpectrum8(eng, waveL, waveR, waveLen, specL, specR, specLen, active)
        }
    }

    override fun onPcm16Stereo(pcm: ShortArray, numFrames: Int, active: Boolean) {
        val eng = engineHandle
        if (eng != 0L && engineInitialized) {
            GeissNative.nativeFeedPcm16Stereo(eng, pcm, numFrames, active)
        }
    }

    override fun applyConfig(snapshot: GeissConfigSnapshot) {
        this.snapshot = snapshot
        val eng = engineHandle
        if (eng != 0L && engineInitialized) {
            GeissNative.nativeUpdateConfig(
                eng,
                snapshot.colorDepthBits,
                snapshot.verticalSizePct,
                snapshot.framesTilAutoSwitch,
                snapshot.gamma,
                snapshot.solarPalFreq,
                snapshot.coarsePalFreq,
                snapshot.volpos,
                snapshot.useBeatDetection,
                snapshot.syncColorToSound,
                snapshot.suppressAllMsg,
                snapshot.disableSongTitlePopups,
                snapshot.randomSongTitleFreq,
                snapshot.songTooltipFrames,
                snapshot.slideShiftFreq,
                snapshot.slideShiftMinFrames,
                snapshot.showFps,
                snapshot.autoDemoWhenSilent
            )
        }
    }

    override fun applyModeRatings(ratings: IntArray) {
        val eng = engineHandle
        if (eng != 0L && engineInitialized) {
            GeissNative.nativeSetAllModeRatings(eng, ratings)
        }
    }

    override fun withEngine(block: (handle: Long, renderW: Int, renderH: Int) -> Unit) {
        val eng = engineHandle
        if (eng != 0L && engineInitialized) {
            block(eng, renderW, renderH)
        }
    }

    override fun getLatestStatus(): GeissNative.EngineStatus? = latestStatus
}
