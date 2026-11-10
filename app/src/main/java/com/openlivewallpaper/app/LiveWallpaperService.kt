package com.openlivewallpaper.app

import android.service.wallpaper.WallpaperService
import android.view.SurfaceHolder

class LiveWallpaperService : WallpaperService() {
    private val weather by lazy { WeatherRepository(this) }

    override fun onCreateEngine(): Engine = SceneEngine()

    inner class SceneEngine : Engine() {
        private lateinit var prefs: Prefs
        private var driver: SceneDriver? = null

        override fun onCreate(surfaceHolder: SurfaceHolder) {
            super.onCreate(surfaceHolder)
            prefs = Prefs(this@LiveWallpaperService)
            setOffsetNotificationsEnabled(true)
            driver = SceneDriver(this@LiveWallpaperService, prefs, weather)
        }

        override fun onSurfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            super.onSurfaceChanged(holder, format, width, height)
            val d = driver ?: return
            d.setSurface(holder.surface, width, height)
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
