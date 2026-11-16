package com.geisswerks.geiss

import android.service.wallpaper.WallpaperService
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.SurfaceHolder
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.egl.EGLSurface
import javax.microedition.khronos.opengles.GL10

class GeissWallpaperService : WallpaperService() {

    override fun onCreateEngine(): Engine = GeissWallpaperEngine()

    inner class GeissWallpaperEngine : Engine() {

        private val controller = GeissController.getInstance(applicationContext)
        private val renderer = GeissGlRenderer(applicationContext)
        private var glThread: GlRenderThread? = null

        private val gestureDetector = GestureDetector(
            applicationContext,
            object : GestureDetector.SimpleOnGestureListener() {
                private fun performConfiguredAction(e: MotionEvent) {
                    val snap = controller.prefs.getSnapshot()
                    when (snap.doubleTapAction) {
                        DoubleTapAction.ACTION_WHEEL -> openActionWheel(e.x, e.y)
                        DoubleTapAction.NEXT_MAP -> controller.nextMap()
                        DoubleTapAction.RANDOM_PALETTE -> controller.randomizePalette()
                        DoubleTapAction.SONG_POPUP -> controller.triggerSongTitlePopup()
                        DoubleTapAction.TOGGLE_LOCK -> controller.toggleLockMode()
                        DoubleTapAction.NONE -> Unit
                    }
                }

                override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
                    if (controller.prefs.getSnapshot().singleTapTriggersAction) {
                        performConfiguredAction(e)
                        return true
                    }
                    return false
                }

                override fun onDoubleTap(e: MotionEvent): Boolean {
                    if (!controller.prefs.getSnapshot().singleTapTriggersAction) {
                        performConfiguredAction(e)
                    }
                    return true
                }
            }
        )

        private fun openActionWheel(x: Float, y: Float) {
            try {
                val intent = android.content.Intent(applicationContext, GeissWheelActivity::class.java).apply {
                    addFlags(
                        android.content.Intent.FLAG_ACTIVITY_NEW_TASK or
                            android.content.Intent.FLAG_ACTIVITY_NO_ANIMATION or
                            android.content.Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS
                    )
                    putExtra(GeissWheelActivity.EXTRA_TAP_X, x)
                    putExtra(GeissWheelActivity.EXTRA_TAP_Y, y)
                }
                applicationContext.startActivity(intent)
            } catch (_: Exception) {
            }
        }

        private var surfW = 1
        private var surfH = 1

        override fun onCreate(surfaceHolder: SurfaceHolder) {
            super.onCreate(surfaceHolder)
            setTouchEventsEnabled(true)
            setOffsetNotificationsEnabled(true)
            glThread = GlRenderThread(surfaceHolder, renderer, controller).apply { start() }
        }

        override fun onSurfaceCreated(holder: SurfaceHolder) {
            super.onSurfaceCreated(holder)
            glThread?.onSurfaceAvailable(holder)
        }

        override fun onSurfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            super.onSurfaceChanged(holder, format, width, height)
            surfW = width.coerceAtLeast(1)
            surfH = height.coerceAtLeast(1)
            glThread?.onWindowResize(surfW, surfH)
        }

        override fun onVisibilityChanged(visible: Boolean) {
            super.onVisibilityChanged(visible)
            renderer.setActive(visible)
            glThread?.onVisibilityChanged(visible)
        }

        override fun onOffsetsChanged(
            xOffset: Float,
            yOffset: Float,
            xOffsetStep: Float,
            yOffsetStep: Float,
            xPixelOffset: Int,
            yPixelOffset: Int
        ) {
            super.onOffsetsChanged(xOffset, yOffset, xOffsetStep, yOffsetStep, xPixelOffset, yPixelOffset)
            // Center 0.5 -> 0.0, edges -> -0.5..0.5
            renderer.setLauncherOffset(xOffset - 0.5f)
        }

        override fun onTouchEvent(event: MotionEvent) {
            gestureDetector.onTouchEvent(event)
            if (event.actionMasked == MotionEvent.ACTION_DOWN ||
                event.actionMasked == MotionEvent.ACTION_MOVE
            ) {
                val nx = (event.x / surfW.toFloat()).coerceIn(0f, 1f)
                val ny = (event.y / surfH.toFloat()).coerceIn(0f, 1f)
                val strength = if (event.actionMasked == MotionEvent.ACTION_DOWN) 1.0f else 0.6f
                renderer.triggerTouchBurst(nx, ny, strength)
            }
            super.onTouchEvent(event)
        }

        override fun onSurfaceDestroyed(holder: SurfaceHolder) {
            glThread?.onSurfaceLost()
            super.onSurfaceDestroyed(holder)
        }

        override fun onDestroy() {
            glThread?.requestExitAndWait()
            glThread = null
            renderer.destroy()
            super.onDestroy()
        }
    }

    private class GlRenderThread(
        private val surfaceHolder: SurfaceHolder,
        private val renderer: GeissGlRenderer,
        private val controller: GeissController
    ) : Thread("GeissWallpaperGL") {

        private val lock = Object()
        private var shouldExit = false
        private var exited = false
        private var hasSurface = false
        private var visible = false
        private var sizeChanged = false
        private var width = 0
        private var height = 0

        private var egl: EGL10? = null
        private var eglDisplay: EGLDisplay? = null
        private var eglConfig: EGLConfig? = null
        private var eglContext: EGLContext? = null
        private var eglSurface: EGLSurface? = null
        private var gl: GL10? = null
        private var forceSdrEgl = false

        override fun run() {
            try {
                guardedRun()
            } finally {
                synchronized(lock) {
                    exited = true
                    lock.notifyAll()
                }
            }
        }

        private fun guardedRun() {
            var needCreateContext = true
            var needCreateSurface = false
            var doSizeChange = false

            while (true) {
                var targetFps = 60
                var shouldSkipFrame = false
                synchronized(lock) {
                    while (!shouldExit && (!hasSurface || !visible || width <= 0 || height <= 0)) {
                        if (!hasSurface && eglSurface != null) {
                            destroySurfaceLocked()
                        }
                        lock.wait()
                    }
                    if (shouldExit) {
                        destroyEglLocked()
                        return
                    }
                    if (needCreateContext || eglContext == null) {
                        if (!initEglContext()) {
                            lock.wait(100)
                            shouldSkipFrame = true
                        } else {
                            needCreateContext = false
                            needCreateSurface = true
                        }
                    }
                    if (!shouldSkipFrame && (needCreateSurface || eglSurface == null)) {
                        if (!createSurfaceLocked()) {
                            if (!forceSdrEgl && controller.prefs.getSnapshot().hdrEnabled) {
                                forceSdrEgl = true
                                destroyEglLocked()
                                needCreateContext = true
                            }
                            lock.wait(50)
                            shouldSkipFrame = true
                        } else {
                            needCreateSurface = false
                            renderer.onSurfaceCreated(gl, eglConfig)
                            doSizeChange = true
                        }
                    }
                    if (!shouldSkipFrame) {
                        if (sizeChanged || doSizeChange) {
                            sizeChanged = false
                            doSizeChange = false
                            renderer.onSurfaceChanged(gl, width, height)
                        }
                        targetFps = controller.prefs.getSnapshot().targetFps.coerceIn(15, 120)
                    }
                }

                if (shouldSkipFrame) {
                    continue
                }

                val frameStartMs = android.os.SystemClock.uptimeMillis()
                renderer.onDrawFrame(gl)

                val ok = swapBuffers()
                if (!ok) {
                    val err = egl?.eglGetError() ?: EGL10.EGL_SUCCESS
                    if (err == 0x300E) { // EGL11.EGL_CONTEXT_LOST
                        synchronized(lock) {
                            destroyEglLocked()
                            needCreateContext = true
                        }
                    } else {
                        synchronized(lock) {
                            destroySurfaceLocked()
                            needCreateSurface = true
                        }
                    }
                }

                val targetFrameMs = (1000L / targetFps).coerceAtLeast(8L)
                val elapsed = android.os.SystemClock.uptimeMillis() - frameStartMs
                val sleepMs = targetFrameMs - elapsed
                if (sleepMs > 1L) {
                    try {
                        sleep(sleepMs)
                    } catch (_: InterruptedException) {
                    }
                }
            }
        }

        private fun initEglContext(): Boolean {
            val e = javax.microedition.khronos.egl.EGLContext.getEGL() as EGL10
            egl = e
            val dpy = e.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY)
            if (dpy == EGL10.EGL_NO_DISPLAY) return false
            val version = IntArray(2)
            if (!e.eglInitialize(dpy, version)) return false
            eglDisplay = dpy

            val hdrAttribs = intArrayOf(
                EGL10.EGL_RENDERABLE_TYPE, 4, // EGL_OPENGL_ES2_BIT
                EGL10.EGL_RED_SIZE, 10,
                EGL10.EGL_GREEN_SIZE, 10,
                EGL10.EGL_BLUE_SIZE, 10,
                EGL10.EGL_ALPHA_SIZE, 2,
                EGL10.EGL_DEPTH_SIZE, 0,
                EGL10.EGL_NONE
            )
            val sdrAttribs = intArrayOf(
                EGL10.EGL_RENDERABLE_TYPE, 4, // EGL_OPENGL_ES2_BIT
                EGL10.EGL_RED_SIZE, 8,
                EGL10.EGL_GREEN_SIZE, 8,
                EGL10.EGL_BLUE_SIZE, 8,
                EGL10.EGL_ALPHA_SIZE, 0,
                EGL10.EGL_DEPTH_SIZE, 0,
                EGL10.EGL_NONE
            )
            val configs = arrayOfNulls<EGLConfig>(1)
            val numConfig = IntArray(1)
            val useHdr = controller.prefs.getSnapshot().hdrEnabled && !forceSdrEgl
            val foundHdr = useHdr &&
                e.eglChooseConfig(dpy, hdrAttribs, configs, 1, numConfig) &&
                numConfig[0] > 0 &&
                configs[0] != null
            if (!foundHdr) {
                if (!e.eglChooseConfig(dpy, sdrAttribs, configs, 1, numConfig) || numConfig[0] <= 0) {
                    return false
                }
            }
            val cfg = configs[0] ?: return false
            eglConfig = cfg

            val ctxAttribs = intArrayOf(0x3098, 2, EGL10.EGL_NONE) // EGL_CONTEXT_CLIENT_VERSION = 2
            val ctx = e.eglCreateContext(dpy, cfg, EGL10.EGL_NO_CONTEXT, ctxAttribs)
            if (ctx == null || ctx == EGL10.EGL_NO_CONTEXT) return false
            eglContext = ctx
            return true
        }

        private fun createSurfaceLocked(): Boolean {
            val e = egl ?: return false
            val dpy = eglDisplay ?: return false
            val cfg = eglConfig ?: return false
            val ctx = eglContext ?: return false

            destroySurfaceLocked()
            val surf = try {
                e.eglCreateWindowSurface(dpy, cfg, surfaceHolder, null)
            } catch (_: Exception) {
                null
            }
            if (surf == null || surf == EGL10.EGL_NO_SURFACE) return false
            if (!e.eglMakeCurrent(dpy, surf, surf, ctx)) {
                e.eglDestroySurface(dpy, surf)
                return false
            }
            eglSurface = surf
            gl = ctx.gl as GL10
            return true
        }

        private fun swapBuffers(): Boolean {
            val e = egl ?: return false
            val dpy = eglDisplay ?: return false
            val surf = eglSurface ?: return false
            return e.eglSwapBuffers(dpy, surf)
        }

        private fun destroySurfaceLocked() {
            val e = egl ?: return
            val dpy = eglDisplay ?: return
            val surf = eglSurface
            if (surf != null && surf != EGL10.EGL_NO_SURFACE) {
                renderer.releaseGlResourcesOnly()
                e.eglMakeCurrent(dpy, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT)
                e.eglDestroySurface(dpy, surf)
                eglSurface = null
            }
        }

        private fun destroyEglLocked() {
            destroySurfaceLocked()
            val e = egl ?: return
            val dpy = eglDisplay
            val ctx = eglContext
            if (dpy != null && ctx != null) {
                e.eglDestroyContext(dpy, ctx)
            }
            if (dpy != null) {
                e.eglTerminate(dpy)
            }
            eglContext = null
            eglDisplay = null
            eglConfig = null
            gl = null
        }

        fun onSurfaceAvailable(@Suppress("UNUSED_PARAMETER") holder: SurfaceHolder) {
            synchronized(lock) {
                hasSurface = true
                lock.notifyAll()
            }
        }

        fun onSurfaceLost() {
            synchronized(lock) {
                hasSurface = false
                lock.notifyAll()
            }
        }

        fun onWindowResize(w: Int, h: Int) {
            synchronized(lock) {
                width = w
                height = h
                sizeChanged = true
                lock.notifyAll()
            }
        }

        fun onVisibilityChanged(vis: Boolean) {
            synchronized(lock) {
                visible = vis
                lock.notifyAll()
            }
        }

        fun requestExitAndWait() {
            synchronized(lock) {
                shouldExit = true
                lock.notifyAll()
                while (!exited) {
                    try {
                        lock.wait()
                    } catch (_: InterruptedException) {
                    }
                }
            }
        }
    }
}
