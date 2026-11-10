package com.openlivewallpaper.app

import android.content.Context
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import kotlin.math.max
import kotlin.math.min

/** Current conditions normalised for the renderer. */
data class Weather(
    val cloud: Float = 0.25f,       // 0..1
    val rain: Float = 0f,           // 0..1 intensity
    val snowfall: Float = 0f,       // 0..1 intensity
    val fog: Float = 0f,            // 0..1
    val wind: Float = 0.25f,        // 0..1.5
    val tempC: Float = 10f,
    val thunder: Boolean = false,
    val snowDepthM: Float = -1f,    // -1 unknown
    val live: Boolean = false,
)

/** Fetches live conditions from Open-Meteo (no API key, no location permission) and caches them. */
class WeatherRepository(ctx: Context) {
    private val sp = ctx.applicationContext.getSharedPreferences("olw_weather", Context.MODE_PRIVATE)
    @Volatile var current: Weather = load()
    @Volatile private var lastFetch = 0L
    @Volatile private var fetching = false

    private fun load(): Weather {
        val s = sp.getString("json", null) ?: return Weather()
        return try { parse(JSONObject(s)).copy(live = false) } catch (e: Exception) { Weather() }
    }

    fun refreshIfStale(city: City, maxAgeMs: Long = 15 * 60_000L) {
        val now = System.currentTimeMillis()
        if (fetching || now - lastFetch < maxAgeMs) return
        fetching = true
        lastFetch = now
        Thread {
            try {
                val url = "https://api.open-meteo.com/v1/forecast?latitude=${city.lat}&longitude=${city.lon}" +
                    "&current=temperature_2m,weather_code,cloud_cover,wind_speed_10m,precipitation,rain,showers,snowfall,visibility" +
                    "&hourly=snow_depth&forecast_hours=1&timezone=auto"
                val c = URL(url).openConnection() as HttpURLConnection
                c.connectTimeout = 8000; c.readTimeout = 8000
                c.setRequestProperty("User-Agent", "OpenLiveWallpaper/1.0")
                val body = c.inputStream.bufferedReader().use { it.readText() }
                val j = JSONObject(body)
                current = parse(j).copy(live = true)
                sp.edit().putString("json", body).apply()
            } catch (e: Exception) {
                lastFetch = now - 15 * 60_000L + 60_000L  // retry in a minute
            } finally { fetching = false }
        }.start()
    }

    companion object {
        fun parse(j: JSONObject): Weather {
            val c = j.getJSONObject("current")
            val code = c.optInt("weather_code", 0)
            val cloud = (c.optDouble("cloud_cover", 25.0) / 100.0).toFloat()
            val rainMm = (c.optDouble("rain", 0.0) + c.optDouble("showers", 0.0)).toFloat()
            val precip = c.optDouble("precipitation", 0.0).toFloat()
            val snowCm = c.optDouble("snowfall", 0.0).toFloat()
            val wind = (c.optDouble("wind_speed_10m", 8.0) / 40.0).toFloat()
            val temp = c.optDouble("temperature_2m", 10.0).toFloat()
            val vis = c.optDouble("visibility", 20000.0)
            val snowy = code in 71..77 || code in 85..86
            val rainy = code in 51..67 || code in 80..82 || code in 95..99
            var rain = if (rainy && !snowy) min(1f, max(rainMm, precip) / 4f).coerceAtLeast(if (code in 51..57) 0.2f else 0.35f) else 0f
            var snow = if (snowy) min(1f, max(snowCm, precip) / 1.5f).coerceAtLeast(0.3f) else 0f
            if (code in 56..57 || code in 66..67) { /* freezing: show as rain */ }
            var fog = if (code == 45 || code == 48) 0.85f else if (vis < 4000) min(1f, ((4000 - vis) / 4000).toFloat()) else 0f
            var depth = -1f
            try {
                val arr = j.getJSONObject("hourly").getJSONArray("snow_depth")
                if (arr.length() > 0 && !arr.isNull(0)) depth = arr.getDouble(0).toFloat()
            } catch (e: Exception) {}
            return Weather(
                cloud = cloud.coerceIn(0f, 1f), rain = rain, snowfall = snow, fog = fog, wind = wind.coerceIn(0f, 1.5f),
                tempC = temp, thunder = code in 95..99, snowDepthM = depth, live = true,
            )
        }
    }
}
