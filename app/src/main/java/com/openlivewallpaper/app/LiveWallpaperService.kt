package com.openlivewallpaper.app

import android.service.wallpaper.WallpaperService
import android.view.SurfaceHolder

class LiveWallpaperService : WallpaperService() {
    private val weather by lazy { WeatherRepository(this) }

    override fun onCreateEngine(): Engine = SceneEngine()

    inner class SceneEngine : Engine() {
        private lateinit var prefs: Prefs
        private var driver: SceneDriver? = null
        private var fixedW = -1
        private var fixedH = -1

        override fun onCreate(surfaceHolder: SurfaceHolder) {
            super.onCreate(surfaceHolder)
            prefs = Prefs(this@LiveWallpaperService)
            setOffsetNotificationsEnabled(true)
            driver = SceneDriver(this@LiveWallpaperService, prefs, weather)
        }

        override fun onSurfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            super.onSurfaceChanged(holder, format, width, height)
            val d = driver ?: return
            val scale = SceneDriver.resolutionScale(d.quality)
            // Render at reduced resolution and let the compositor upscale: big battery and GPU win.
            if (width == fixedW && height == fixedH) {
                d.setSurface(holder.surface, width, height)
                return
            }
            fixedW = (width * scale).toInt().coerceAtLeast(2)
            fixedH = (height * scale).toInt().coerceAtLeast(2)
            holder.setFixedSize(fixedW, fixedH)
        }

        override fun onSurfaceDestroyed(holder: SurfaceHolder) {
            driver?.setSurface(null, 0, 0)
            super.onSurfaceDestroyed(holder)
        }

        override fun onVisibilityChanged(visible: Boolean) {
            driver?.setVisible(visible)
        }

        override fun onOffsetsChanged(xOffset: Float, yOffset: Float, xStep: Float, yStep: Float, xPixels: Int, yPixels: Int) {
            driver?.scroll = if (xStep > 0f) xOffset else 0.5f
        }

        override fun onDestroy() {
            driver?.destroy()
            driver = null
            super.onDestroy()
        }
    }
}
