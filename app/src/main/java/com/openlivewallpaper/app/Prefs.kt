package com.openlivewallpaper.app

import android.content.Context
import android.content.SharedPreferences

/** All user settings and preview overrides, stored in one SharedPreferences file. */
class Prefs(ctx: Context) {
    /** Preview overrides only apply where this is true (the in-app preview), never to the real wallpaper. */
    var allowPreview = false
    val sp: SharedPreferences = ctx.applicationContext.getSharedPreferences("olw", Context.MODE_PRIVATE)

    var city: String get() = sp.getString("city", "Montréal")!!; set(v) = sp.edit().putString("city", v).apply()
    /** 0 = auto, 1 = low, 2 = medium, 3 = high */
    var quality: Int get() = sp.getInt("quality", 0); set(v) = sp.edit().putInt("quality", v).apply()
    var fps60: Boolean get() = sp.getBoolean("fps60", false); set(v) = sp.edit().putBoolean("fps60", v).apply()
    var eventsOn: Boolean get() = sp.getBoolean("events", true); set(v) = sp.edit().putBoolean("events", v).apply()
    var auroraMode: Int get() = sp.getInt("aurora", 0); set(v) = sp.edit().putInt("aurora", v).apply() // 0 auto, 1 always, 2 never
    var liveWeather: Boolean get() = sp.getBoolean("liveWeather", true); set(v) = sp.edit().putBoolean("liveWeather", v).apply()
    var scrollEffect: Boolean get() = sp.getBoolean("scroll", true); set(v) = sp.edit().putBoolean("scroll", v).apply()

    // ---- preview overrides (used by the in-app preview and, when enabled, by the wallpaper) ----
    var previewOn: Boolean get() = sp.getBoolean("pv_on", false); set(v) = sp.edit().putBoolean("pv_on", v).apply()
    /** -1 auto, 0 winter, 1 spring, 2 summer, 3 autumn */
    var pvSeason: Int get() = sp.getInt("pv_season", -1); set(v) = sp.edit().putInt("pv_season", v).apply()
    /** -1 = real time, otherwise hour of day 0..24 */
    var pvHour: Float get() = sp.getFloat("pv_hour", -1f); set(v) = sp.edit().putFloat("pv_hour", v).apply()
    /** 0 auto (real), 1 clear, 2 partly, 3 overcast, 4 rain, 5 storm, 6 snow, 7 blizzard, 8 fog */
    var pvWeather: Int get() = sp.getInt("pv_weather", 0); set(v) = sp.edit().putInt("pv_weather", v).apply()
    /** -1 auto by date, 0 none, otherwise event bit */
    var pvEvent: Int get() = sp.getInt("pv_event", -1); set(v) = sp.edit().putInt("pv_event", v).apply()
    var pvSpeed: Int get() = sp.getInt("pv_speed", 1); set(v) = sp.edit().putInt("pv_speed", v).apply()
    var pvAurora: Boolean get() = sp.getBoolean("pv_aurora", false); set(v) = sp.edit().putBoolean("pv_aurora", v).apply()

    fun resetPreview() {
        sp.edit().putBoolean("pv_on", false).putInt("pv_season", -1).putFloat("pv_hour", -1f).putInt("pv_weather", 0)
            .putInt("pv_event", -1).putInt("pv_speed", 1).putBoolean("pv_aurora", false).apply()
    }
}
