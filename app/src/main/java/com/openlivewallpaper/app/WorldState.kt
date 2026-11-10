package com.openlivewallpaper.app

import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** Turns the clock, the city, the weather and the preview overrides into the renderer parameter block. */
class WorldState(private val prefs: Prefs, private val weather: WeatherRepository) {
    private val params = FloatArray(NativeRenderer.P_COUNT)

    // Simulated clock used by preview / time-lapse. realBase is the real moment simulation started.
    private var simMs = 0L
    private var simActive = false
    private var lastRealMs = 0L

    private fun smooth(a: Float, b: Float, x: Float): Float { val t = ((x - a) / (b - a)).coerceIn(0f, 1f); return t * t * (3 - 2 * t) }

    private fun doyFactors(doy: Int, out: FloatArray) {
        val d = doy.toFloat()
        // leaf amount: bare until early May, full by late May, falls mid Oct to early Nov
        out[0] = smooth(120f, 148f, d) * (1f - smooth(282f, 312f, d))
        // autumn colour
        out[1] = smooth(258f, 284f, d) * (1f - smooth(302f, 318f, d))
        // seasonal snow cover baseline (Montréal): ramps up late Nov, melts late Mar to mid Apr
        out[2] = if (d > 200) smooth(322f, 346f, d) else (1f - smooth(75f, 108f, d))
        // stream ice
        out[3] = if (d > 200) smooth(335f, 360f, d) else (1f - smooth(70f, 100f, d))
        // stream flow: spring melt peak, low in winter
        out[4] = when {
            d < 85 -> 0.3f
            d < 150 -> 0.3f + 0.7f * smooth(85f, 110f, d) * (1f - 0.3f * smooth(130f, 150f, d))
            d < 270 -> 0.55f - 0.12f * smooth(160f, 220f, d)
            else -> 0.5f - 0.2f * smooth(300f, 345f, d)
        }
    }

    fun compute(realNowMs: Long, dtSec: Float, scroll: Float): FloatArray {
        val city = Cities.byName(prefs.city)
        val zone = ZoneId.of(city.zone)
        val pv = prefs.previewOn && prefs.allowPreview

        // ----- clock -----
        if (pv && (prefs.pvSeason >= 0 || prefs.pvHour >= 0f || prefs.pvSpeed > 1)) {
            if (!simActive) { simActive = true; simMs = realNowMs; lastRealMs = realNowMs }
            val dt = (realNowMs - lastRealMs).coerceIn(0, 1000)
            simMs += dt * prefs.pvSpeed
            lastRealMs = realNowMs
        } else { simActive = false }
        var ms = if (simActive) simMs else realNowMs
        var zdt = Instant.ofEpochMilli(ms).atZone(zone)
        if (pv && prefs.pvSeason >= 0) {
            val (mo, da) = when (prefs.pvSeason) { 0 -> 1 to 20; 1 -> 4 to 22; 2 -> 7 to 15; else -> 10 to 8 }
            val base = zdt.withMonth(mo).withDayOfMonth(da)
            zdt = base  // keeps the time of day (time-lapse continues from there)
            ms = zdt.toInstant().toEpochMilli()
        }
        if (pv && prefs.pvHour >= 0f && prefs.pvSpeed <= 1) {
            val h = prefs.pvHour.toInt().coerceIn(0, 23)
            val mi = ((prefs.pvHour - h) * 60).toInt().coerceIn(0, 59)
            zdt = zdt.withHour(h).withMinute(mi).withSecond(0).withNano(0)
            ms = zdt.toInstant().toEpochMilli()
        }
        val hour = zdt.hour + zdt.minute / 60f + zdt.second / 3600f
        val doy = zdt.dayOfYear
        val southern = city.lat < 0

        // ----- astronomy -----
        val (sunP, sunLon) = Astronomy.sun(ms, city.lat, city.lon)
        val (moonP, moonLon) = Astronomy.moon(ms, city.lat, city.lon)
        Astronomy.toWorld(sunP, params, NativeRenderer.P_SUNX)
        Astronomy.toWorld(moonP, params, NativeRenderer.P_MOONX)
        val phase = (((moonLon - sunLon) % 360.0 + 360.0) % 360.0 / 360.0).toFloat()
        params[NativeRenderer.P_MOONPHASE] = phase

        // ----- season & vegetation -----
        var s = (((doy - 355 + 365) % 365) / 365f) * 4f
        if (southern) s = (s + 2f) % 4f
        val f = FloatArray(5)
        doyFactors(if (southern) (doy + 182) % 365 else doy, f)
        val w = when {
            pv && prefs.pvWeather != 0 -> forcedWeather(prefs.pvWeather)
            prefs.liveWeather -> weather.current
            else -> Weather()
        }
        // snow cover: real snow depth if known, else seasonal baseline refined by temperature
        var snow = f[2]
        if (w.live && w.snowDepthM >= 0f && !(pv && (prefs.pvSeason >= 0 || prefs.pvWeather != 0))) {
            snow = max(smooth(0.0f, 0.12f, w.snowDepthM), if (w.snowfall > 0.2f) 0.5f else 0f)
        } else if (w.tempC > 5f) snow *= (1f - smooth(5f, 14f, w.tempC)).coerceIn(0f, 1f)
        if (w.snowfall > 0.2f) snow = max(snow, 0.35f + 0.65f * w.snowfall)
        if (pv && prefs.pvWeather in 6..7) snow = max(snow, 0.7f)
        snow = snow.coerceIn(0f, 1f)
        var ice = f[3]
        if (w.tempC > 6f && !(pv && prefs.pvSeason >= 0)) ice *= (1f - smooth(6f, 14f, w.tempC))
        if (snow < 0.05f) ice = min(ice, 0.2f)

        // ----- events -----
        var ev = if (prefs.eventsOn) Events.forDate(zdt.toLocalDate()) else 0
        if (pv && prefs.pvEvent >= 0) {
            ev = prefs.pvEvent
            if (ev and (Events.NEWYEAR or Events.QUEBEC or Events.CANADA) != 0) ev = ev or Events.FIREWORKS
        }

        // ----- aurora -----
        val night = sunP.altitude < -0.2
        var aurora = 0f
        if (night && w.cloud < 0.6f) {
            val h = ((zdt.toLocalDate().toEpochDay() * 2654435761L) ushr 7).toInt() and 7
            val inSeason = doy < 130 || doy > 240
            if (prefs.auroraMode == 1 || (prefs.auroraMode == 0 && inSeason && h == 0)) aurora = 1f
        }
        if (pv && prefs.pvAurora) aurora = 1f

        // ----- lightning -----
        var lightning = 0f
        val tt = (realNowMs % 100000L) / 1000f
        if (w.thunder || (pv && prefs.pvWeather == 5)) {
            val k = (tt * 0.37f) % 1f
            lightning = if (k < 0.05f) (1f - k / 0.05f) else if (k in 0.12f..0.15f) 0.6f else 0f
        }

        params[NativeRenderer.P_TIME] = ((realNowMs % 3_600_000L) / 1000.0).toFloat()
        params[NativeRenderer.P_SEASON] = s
        params[NativeRenderer.P_SNOW] = snow
        params[NativeRenderer.P_AUTUMN] = f[1]
        params[NativeRenderer.P_LEAF] = f[0]
        params[NativeRenderer.P_CLOUD] = w.cloud
        params[NativeRenderer.P_RAIN] = w.rain
        params[NativeRenderer.P_SNOWFALL] = w.snowfall
        params[NativeRenderer.P_FOG] = w.fog
        params[NativeRenderer.P_WIND] = w.wind
        params[NativeRenderer.P_LIGHTNING] = lightning
        params[NativeRenderer.P_ICE] = ice
        params[NativeRenderer.P_AURORA] = aurora
        params[NativeRenderer.P_EVENT] = ev.toFloat()
        params[NativeRenderer.P_TEMP] = w.tempC
        params[NativeRenderer.P_SCROLL] = scroll
        params[NativeRenderer.P_DAYOFYEAR] = doy.toFloat()
        params[NativeRenderer.P_HOUR] = hour
        params[NativeRenderer.P_FLOW] = f[4] * (1f + 0.5f * w.rain)
        params[NativeRenderer.P_DT] = dtSec
        return params
    }

    private fun forcedWeather(k: Int): Weather = when (k) {
        1 -> Weather(cloud = 0.05f, wind = 0.2f, tempC = 12f)
        2 -> Weather(cloud = 0.5f, wind = 0.3f, tempC = 12f)
        3 -> Weather(cloud = 0.97f, wind = 0.3f, tempC = 8f)
        4 -> Weather(cloud = 0.95f, rain = 0.6f, wind = 0.4f, tempC = 9f)
        5 -> Weather(cloud = 1f, rain = 1f, wind = 1.0f, tempC = 14f, thunder = true)
        6 -> Weather(cloud = 0.9f, snowfall = 0.55f, wind = 0.25f, tempC = -4f)
        7 -> Weather(cloud = 1f, snowfall = 1f, wind = 1.3f, fog = 0.35f, tempC = -12f)
        8 -> Weather(cloud = 0.7f, fog = 0.9f, wind = 0.1f, tempC = 4f)
        else -> Weather()
    }
}
