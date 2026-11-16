package com.geisswerks.geiss

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.audiofx.Visualizer
import android.media.projection.MediaProjection
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import java.util.concurrent.CopyOnWriteArraySet
import kotlin.concurrent.thread
import kotlin.math.hypot
import kotlin.math.min

object GeissAudioManager {
    private const val TAG = "GeissAudioManager"
    private const val SAMPLE_RATE = 44100

    interface AudioConsumer {
        fun onWaveAndSpectrum8(
            waveL: ByteArray?,
            waveR: ByteArray?,
            waveLen: Int,
            specL: ByteArray?,
            specR: ByteArray?,
            specLen: Int,
            active: Boolean
        )

        fun onPcm16Stereo(pcm: ShortArray, numFrames: Int, active: Boolean)
    }

    enum class CaptureStatus(val message: String) {
        IDLE("Paused"),
        STREAMING_OUTPUT_MIX("Live: Global Output Mix"),
        STREAMING_PLAYBACK("Live: System Audio Capture"),
        STREAMING_MIC("Live: Microphone"),
        NEEDS_RECORD_AUDIO("Needs Microphone / Audio Permission"),
        NEEDS_PROJECTION("Tap Audio Source to Grant System Capture"),
        ERROR("Audio Input Unavailable (Auto-Demo)")
    }

    private val consumers = CopyOnWriteArraySet<AudioConsumer>()
    private val lock = Any()

    @Volatile
    private var appContext: Context? = null

    @Volatile
    private var desiredSource: AudioSourceMode = AudioSourceMode.OUTPUT_MIX

    @Volatile
    var status: CaptureStatus = CaptureStatus.IDLE
        private set

    private var statusListener: ((CaptureStatus) -> Unit)? = null

    private var visualizer: Visualizer? = null
    private var audioRecord: AudioRecord? = null
    private var mediaProjection: MediaProjection? = null

    @Volatile
    private var workerRunning = false
    private var workerThread: Thread? = null

    fun init(context: Context) {
        appContext = context.applicationContext
    }

    fun setStatusListener(listener: ((CaptureStatus) -> Unit)?) {
        statusListener = listener
        listener?.invoke(status)
    }

    private fun updateStatus(newStatus: CaptureStatus) {
        if (status != newStatus) {
            status = newStatus
            statusListener?.invoke(newStatus)
        }
    }

    fun hasRecordAudioPermission(context: Context): Boolean {
        return ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun hasMediaProjection(): Boolean = mediaProjection != null

    fun setMediaProjection(projection: MediaProjection?) {
        synchronized(lock) {
            if (mediaProjection !== projection) {
                mediaProjection?.stop()
                mediaProjection = projection
                projection?.registerCallback(object : MediaProjection.Callback() {
                    override fun onStop() {
                        synchronized(lock) {
                            if (mediaProjection === projection) {
                                mediaProjection = null
                                if (desiredSource == AudioSourceMode.PLAYBACK_CAPTURE) {
                                    restartLocked()
                                }
                            }
                        }
                    }
                }, android.os.Handler(android.os.Looper.getMainLooper()))
            }
            if (consumers.isNotEmpty()) {
                restartLocked()
            }
        }
    }

    fun setAudioSource(mode: AudioSourceMode) {
        synchronized(lock) {
            if (desiredSource != mode) {
                desiredSource = mode
                if (consumers.isNotEmpty()) {
                    restartLocked()
                }
            }
        }
    }

    fun registerConsumer(consumer: AudioConsumer) {
        synchronized(lock) {
            val wasEmpty = consumers.isEmpty()
            consumers.add(consumer)
            if (wasEmpty || (!workerRunning && visualizer == null)) {
                startLocked()
            }
        }
    }

    fun unregisterConsumer(consumer: AudioConsumer) {
        synchronized(lock) {
            consumers.remove(consumer)
            if (consumers.isEmpty()) {
                stopLocked()
            }
        }
    }

    fun refreshCapture() {
        synchronized(lock) {
            if (consumers.isNotEmpty()) {
                restartLocked()
            }
        }
    }

    private fun restartLocked() {
        stopLocked()
        startLocked()
    }

    private fun startLocked() {
        val ctx = appContext ?: return
        if (!hasRecordAudioPermission(ctx)) {
            updateStatus(CaptureStatus.NEEDS_RECORD_AUDIO)
            notifyInactive()
            return
        }

        when (desiredSource) {
            AudioSourceMode.OUTPUT_MIX -> {
                if (!startOutputMixLocked()) {
                    updateStatus(CaptureStatus.ERROR)
                    notifyInactive()
                }
            }

            AudioSourceMode.PLAYBACK_CAPTURE -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && mediaProjection != null) {
                    if (!startPlaybackCaptureLocked()) {
                        // Fallback gracefully to Output Mix
                        if (!startOutputMixLocked()) {
                            updateStatus(CaptureStatus.ERROR)
                            notifyInactive()
                        }
                    }
                } else {
                    // Fallback temporarily to Output Mix while signaling projection prompt needed
                    startOutputMixLocked()
                    updateStatus(CaptureStatus.NEEDS_PROJECTION)
                }
            }

            AudioSourceMode.MICROPHONE -> {
                if (!startMicrophoneLocked()) {
                    updateStatus(CaptureStatus.ERROR)
                    notifyInactive()
                }
            }
        }
    }

    private fun stopLocked() {
        workerRunning = false
        workerThread?.let { t ->
            try {
                t.interrupt()
                t.join(350)
            } catch (_: InterruptedException) {
            }
        }
        workerThread = null

        try {
            visualizer?.enabled = false
            visualizer?.release()
        } catch (_: Exception) {
        }
        visualizer = null

        try {
            audioRecord?.stop()
        } catch (_: Exception) {
        }
        try {
            audioRecord?.release()
        } catch (_: Exception) {
        }
        audioRecord = null

        if (consumers.isEmpty()) {
            updateStatus(CaptureStatus.IDLE)
        }
    }

    private fun startOutputMixLocked(): Boolean {
        return try {
            val range = Visualizer.getCaptureSizeRange()
            val captureSize = min(1024, range[1]).coerceAtLeast(range[0])
            val vis = Visualizer(0)
            vis.captureSize = captureSize
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                vis.scalingMode = Visualizer.SCALING_MODE_NORMALIZED
            }
            vis.enabled = true
            visualizer = vis

            val waveBuf = ByteArray(captureSize)
            val fftBuf = ByteArray(captureSize)
            val specMag = ByteArray(captureSize / 2)

            workerRunning = true
            updateStatus(CaptureStatus.STREAMING_OUTPUT_MIX)
            workerThread = thread(name = "GeissVisThread", isDaemon = true) {
                while (workerRunning) {
                    val v = visualizer
                    if (v != null && v.enabled) {
                        val wRes = try {
                            v.getWaveForm(waveBuf)
                        } catch (_: Exception) {
                            Visualizer.ERROR
                        }
                        val fRes = try {
                            v.getFft(fftBuf)
                        } catch (_: Exception) {
                            Visualizer.ERROR
                        }
                        if (wRes == Visualizer.SUCCESS) {
                            if (fRes == Visualizer.SUCCESS) {
                                convertVisualizerFftToMagnitude(fftBuf, specMag)
                            }
                            for (c in consumers) {
                                c.onWaveAndSpectrum8(
                                    waveBuf,
                                    waveBuf,
                                    captureSize,
                                    specMag,
                                    specMag,
                                    specMag.size,
                                    true
                                )
                            }
                        }
                    }
                    try {
                        Thread.sleep(16)
                    } catch (_: InterruptedException) {
                        break
                    }
                }
            }
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start Visualizer(0)", e)
            try {
                visualizer?.release()
            } catch (_: Exception) {
            }
            visualizer = null
            false
        }
    }

    @SuppressLint("MissingPermission")
    private fun startPlaybackCaptureLocked(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
        val proj = mediaProjection ?: return false
        return try {
            val config = AudioPlaybackCaptureConfiguration.Builder(proj)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()

            val format = AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                .build()

            val minBuf = AudioRecord.getMinBufferSize(
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_STEREO,
                AudioFormat.ENCODING_PCM_16BIT
            ).coerceAtLeast(4096)

            val record = AudioRecord.Builder()
                .setAudioFormat(format)
                .setBufferSizeInBytes(minBuf * 2)
                .setAudioPlaybackCaptureConfig(config)
                .build()

            if (record.state != AudioRecord.STATE_INITIALIZED) {
                record.release()
                return false
            }

            record.startRecording()
            audioRecord = record
            startPcmReaderThread(record, stereo = true, CaptureStatus.STREAMING_PLAYBACK)
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start AudioPlaybackCapture", e)
            false
        }
    }

    @SuppressLint("MissingPermission")
    private fun startMicrophoneLocked(): Boolean {
        return try {
            var channelConfig = AudioFormat.CHANNEL_IN_STEREO
            var minBuf = AudioRecord.getMinBufferSize(
                SAMPLE_RATE,
                channelConfig,
                AudioFormat.ENCODING_PCM_16BIT
            )
            var isStereo = true
            if (minBuf <= 0) {
                channelConfig = AudioFormat.CHANNEL_IN_MONO
                isStereo = false
                minBuf = AudioRecord.getMinBufferSize(
                    SAMPLE_RATE,
                    channelConfig,
                    AudioFormat.ENCODING_PCM_16BIT
                )
            }
            val record = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                SAMPLE_RATE,
                channelConfig,
                AudioFormat.ENCODING_PCM_16BIT,
                minBuf.coerceAtLeast(4096) * 2
            )
            if (record.state != AudioRecord.STATE_INITIALIZED) {
                record.release()
                return false
            }
            record.startRecording()
            audioRecord = record
            startPcmReaderThread(record, stereo = isStereo, CaptureStatus.STREAMING_MIC)
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start Microphone AudioRecord", e)
            false
        }
    }

    private fun startPcmReaderThread(record: AudioRecord, stereo: Boolean, liveStatus: CaptureStatus) {
        workerRunning = true
        updateStatus(liveStatus)
        val numFrames = 1024
        val readBuf = ShortArray(if (stereo) numFrames * 2 else numFrames)
        val stereoBuf = ShortArray(numFrames * 2)

        workerThread = thread(name = "GeissPcmThread", isDaemon = true) {
            while (workerRunning) {
                val read = try {
                    record.read(readBuf, 0, readBuf.size)
                } catch (_: Exception) {
                    -1
                }
                if (read > 0) {
                    if (stereo) {
                        val frames = read / 2
                        for (c in consumers) {
                            c.onPcm16Stereo(readBuf, frames, true)
                        }
                    } else {
                        for (i in 0 until read) {
                            val s = readBuf[i]
                            stereoBuf[i * 2] = s
                            stereoBuf[i * 2 + 1] = s
                        }
                        for (c in consumers) {
                            c.onPcm16Stereo(stereoBuf, read, true)
                        }
                    }
                } else {
                    try {
                        Thread.sleep(10)
                    } catch (_: InterruptedException) {
                        break
                    }
                }
            }
        }
    }

    private fun notifyInactive() {
        for (c in consumers) {
            c.onWaveAndSpectrum8(null, null, 0, null, null, 0, false)
        }
    }

    internal fun convertVisualizerFftToMagnitude(fft: ByteArray, outMag: ByteArray) {
        val bins = outMag.size
        if (bins == 0 || fft.size < 2) return
        // Android Visualizer FFT layout: [DC_re, Nyq_re, Re(1), Im(1), Re(2), Im(2), ...]
        val dc = kotlin.math.abs(fft[0].toInt()).coerceIn(0, 255)
        outMag[0] = dc.toByte()
        for (k in 1 until bins) {
            val idx = k * 2
            if (idx + 1 >= fft.size) break
            val re = fft[idx].toFloat()
            val im = fft[idx + 1].toFloat()
            val mag = (hypot(re, im) * 2.0f).toInt().coerceIn(0, 255)
            outMag[k] = mag.toByte()
        }
    }
}
