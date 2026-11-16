package com.geisswerks.geiss

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GeissPreferencesTest {

    @Test
    fun presetJsonRoundTripPreservesAllFields() {
        val original = PresetState(
            name = "Deep Space Vortex",
            valid = true,
            xCenter = 0.42f,
            yCenter = 0.63f,
            mode = 19,
            waveform = 5,
            damping = 0.992f,
            magic = 254,
            shift = true,
            gamma = 24,
            volpos = 14,
            t1 = 0.023f,
            t2 = -0.015f,
            s1 = 0.975f,
            s2 = 0.942f,
            f1 = 0.955f,
            f2 = 0.004f,
            f3 = 0.041f,
            f4 = 0.062f,
            effects = intArrayOf(1, -1, 1, 1, -1, 1, -1, 1),
            spectrum = true,
            loBand = 3,
            hiBand = 12,
            bFxPalette = true,
            iFxPaletteNum = 2,
            c1 = 5,
            c2 = 8,
            c3 = 11
        )

        val json = original.toJson()
        val parsed = PresetState.fromJson(json)
        assertNotNull(parsed)
        assertEquals(original, parsed)
        assertEquals(original.xCenter, parsed!!.xCenter, 1e-4f)
        assertEquals(original.t2, parsed.t2, 1e-4f)
        assertArrayEquals(original.effects, parsed.effects)
    }

    @Test
    fun presetNativeArraysRoundTripMatchesOriginal() {
        val original = PresetState(
            name = "Phonic Spectrum",
            mode = 22,
            waveform = 6,
            effects = intArrayOf(1, 1, -1, -1, 1, -1, -1, 1),
            spectrum = true,
            bFxPalette = false,
            c1 = 2,
            c2 = 7,
            c3 = 13
        )
        val (floats, ints, bools) = original.toNativeArrays()
        val reconstructed = PresetState.fromNativeArrays(original.name, floats, ints, bools)
        assertEquals(original, reconstructed)
    }

    @Test
    fun malformedPresetJsonReturnsNullSafely() {
        assertNull(PresetState.fromJson("{not valid json"))
    }

    @Test
    fun computeRenderSizeRespectsPortraitAndLandscapeAspectsAndEvenAlignment() {
        val (pw, ph) = GeissPreferences.computeRenderSize(1080, 2400, RenderResolution.MEDIUM)
        assertEquals(480, pw)
        assertEquals(1066, ph)
        assertEquals(0, pw % 2)
        assertEquals(0, ph % 2)

        val (lw, lh) = GeissPreferences.computeRenderSize(2400, 1080, RenderResolution.LOW)
        assertEquals(320, lh)
        assertEquals(710, lw)
        assertEquals(0, lw % 2)
        assertEquals(0, lh % 2)
    }

    @Test
    fun enumParsingFallsBackGracefullyOnUnknownKeys() {
        assertEquals(AudioSourceMode.OUTPUT_MIX, AudioSourceMode.fromKey("BAD_KEY"))
        assertEquals(AudioSourceMode.PLAYBACK_CAPTURE, AudioSourceMode.fromKey("PLAYBACK_CAPTURE"))
        assertEquals(RenderResolution.MEDIUM, RenderResolution.fromKey(null))
        assertEquals(DoubleTapAction.NEXT_MAP, DoubleTapAction.fromKey("UNKNOWN"))
    }

    @Test
    fun visualizerFftConversionHandlesPackedComplexLayout() {
        // DC=50, Nyq=0, bin1=(30, 40) -> hypot=50 * 2 = 100
        val fft = byteArrayOf(50, 0, 30, 40, -60, 0)
        val mag = ByteArray(3)
        GeissAudioManager.convertVisualizerFftToMagnitude(fft, mag)
        assertEquals(50, mag[0].toInt() and 0xFF)
        assertEquals(100, mag[1].toInt() and 0xFF)
        assertEquals(120, mag[2].toInt() and 0xFF)
        assertFalse(mag.all { it.toInt() == 0 })
        assertTrue((mag[1].toInt() and 0xFF) == 100)
    }
}
