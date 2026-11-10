package com.openlivewallpaper.app

import android.app.ActivityManager
import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import android.view.Choreographer
import android.view.Surface

/**
 * Owns the native renderer and the render thread. Used by both the wallpaper engine and the in-app preview.
 * Everything that touches the native handle runs on the render thread.
 */
class SceneDriver(private val ctx: Context, private val prefs: Prefs, private val weather: WeatherRepository) {
    private val thread = HandlerThread("olw-render").apply { start() }
    private val handler = Handler(thread.looper)
    private val world = WorldState(prefs, weather)
    private var handle = 0L
    private var surface: Surface? = null
    @Volatile private var visible = false
    @Volatile var scroll = 0.5f
    private var lastFrameNs = 0L
    private var lastDrawNs = 0L
    private var running = false
    private var frames = 0
    private var fpsStamp = SystemClock.elapsedRealtime()
    @Volatile var measuredFps = 0f
    private var created = false
    @Volatile var failed = false

    companion object {
        fun effectiveQuality(ctx: Context, prefs: Prefs): Int {
            if (prefs.quality in 1..3) return prefs.quality - 1
            val mi = ActivityManager.MemoryInfo()
            (ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).getMemoryInfo(mi)
            val gb = mi.totalMem / (1024.0 * 1024 * 1024)
            return if (gb >= 11.0) 2 else 1
        }
        fun resolutionScale(q: Int) = when (q) { 0 -> 0.5f; 1 -> 0.65f; else -> 0.85f }
    }

    val quality get() = effectiveQuality(ctx, prefs)

    private val frameCallback = Choreographer.FrameCallback { ns -> onFrame(ns) }

    private fun ensureRenderer(): Boolean {
        if (created) return handle != 0L
        created = true
        val t0 = SystemClock.elapsedRealtime()
        handle = NativeRenderer.nCreate(ctx.assets, quality)
        Log.i("OLW", "renderer created in ${SystemClock.elapsedRealtime() - t0} ms, handle=$handle")
        if (handle == 0L) failed = true
        return handle != 0L
    }

    fun setSurface(s: Surface?, w: Int, h: Int) {
        handler.post {
            surface = s
            if (s == null) {
                if (handle != 0L) NativeRenderer.nSetSurface(handle, null, 0, 0)
                stopLoop()
            } else if (ensureRenderer()) {
                NativeRenderer.nSetSurface(handle, s, w, h)
                if (visible) startLoop()
            }
        }
    }

    fun setVisible(v: Boolean) {
        visible = v
        handler.post {
            if (v) { weather.refreshIfStale(Cities.byName(prefs.city)); if (surface != null && handle != 0L) startLoop() }
            else stopLoop()
        }
    }

    private fun startLoop() {
        if (running) return
        running = true
        lastFrameNs = 0
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    private fun stopLoop() {
        running = false
        Choreographer.getInstance().removeFrameCallback(frameCallback)
    }

    private fun onFrame(ns: Long) {
        if (!running) return
        Choreographer.getInstance().postFrameCallback(frameCallback)
        val intervalNs = if (prefs.fps60) 16_000_000L else 32_000_000L
        if (lastDrawNs != 0L && ns - lastDrawNs < intervalNs - 3_000_000L) return
        val dt = if (lastDrawNs == 0L) 0.033f else ((ns - lastDrawNs) / 1e9f).coerceAtMost(0.2f)
        lastDrawNs = ns
        weather.refreshIfStale(Cities.byName(prefs.city))
        val sc = if (prefs.scrollEffect) scroll else 0.5f
        val p = world.compute(System.currentTimeMillis(), dt, sc)
        NativeRenderer.nDraw(handle, p)
        frames++
        val now = SystemClock.elapsedRealtime()
        if (now - fpsStamp >= 2000) {
            measuredFps = frames * 1000f / (now - fpsStamp)
            Log.i("OLW", "fps %.1f".format(measuredFps))
            frames = 0; fpsStamp = now
        }
    }

    fun destroy() {
        handler.post {
            stopLoop()
            if (handle != 0L) { NativeRenderer.nDestroy(handle); handle = 0 }
            thread.quitSafely()
        }
    }
}
