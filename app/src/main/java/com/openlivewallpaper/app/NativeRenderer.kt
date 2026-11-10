package com.openlivewallpaper.app

import android.content.res.AssetManager
import android.view.Surface

/** Thin JNI wrapper around the native Vulkan renderer. */
object NativeRenderer {
    init { System.loadLibrary("olw") }

    @JvmStatic external fun nCreate(assets: AssetManager, quality: Int): Long
    @JvmStatic external fun nDestroy(handle: Long)
    @JvmStatic external fun nSetSurface(handle: Long, surface: Surface?, w: Int, h: Int): Boolean
    @JvmStatic external fun nDraw(handle: Long, params: FloatArray)

    // Parameter indices (keep in sync with common.h)
    const val P_TIME = 0; const val P_SUNX = 1; const val P_MOONX = 4; const val P_MOONPHASE = 7
    const val P_SEASON = 8; const val P_SNOW = 9; const val P_AUTUMN = 10; const val P_LEAF = 11
    const val P_CLOUD = 12; const val P_RAIN = 13; const val P_SNOWFALL = 14; const val P_FOG = 15
    const val P_WIND = 16; const val P_LIGHTNING = 17; const val P_ICE = 18; const val P_AURORA = 19
    const val P_EVENT = 20; const val P_TEMP = 21; const val P_SCROLL = 22; const val P_QUALITY = 23
    const val P_DAYOFYEAR = 24; const val P_HOUR = 25; const val P_FLOW = 26; const val P_DT = 27
    const val P_COUNT = 32
}
