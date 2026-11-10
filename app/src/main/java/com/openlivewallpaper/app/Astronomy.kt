package com.openlivewallpaper.app

import kotlin.math.*

/** Low-precision sun and moon positions, good to a fraction of a degree: plenty for a wallpaper. */
object Astronomy {
    data class Pos(val altitude: Double, val azimuth: Double) // radians, azimuth from north clockwise

    private fun norm360(x: Double) = ((x % 360.0) + 360.0) % 360.0
    private const val D2R = PI / 180.0

    private fun toHorizontal(ra: Double, dec: Double, jd: Double, latDeg: Double, lonDeg: Double): Pos {
        val n = jd - 2451545.0
        val gmst = norm360(280.46061837 + 360.98564736629 * n)
        val ha = (gmst + lonDeg) * D2R - ra
        val lat = latDeg * D2R
        val alt = asin(sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha))
        val az = atan2(sin(ha), cos(ha) * sin(lat) - tan(dec) * cos(lat)) + PI
        return Pos(alt, az)
    }

    fun jd(epochMs: Long) = epochMs / 86400000.0 + 2440587.5

    /** Returns sun position and ecliptic longitude (deg). */
    fun sun(epochMs: Long, lat: Double, lon: Double): Pair<Pos, Double> {
        val jd = jd(epochMs)
        val n = jd - 2451545.0
        val l = norm360(280.460 + 0.9856474 * n)
        val g = norm360(357.528 + 0.9856003 * n) * D2R
        val lam = (l + 1.915 * sin(g) + 0.020 * sin(2 * g))
        val eps = (23.439 - 0.0000004 * n) * D2R
        val lr = lam * D2R
        val ra = atan2(cos(eps) * sin(lr), cos(lr))
        val dec = asin(sin(eps) * sin(lr))
        return toHorizontal(ra, dec, jd, lat, lon) to norm360(lam)
    }

    /** Returns moon position and ecliptic longitude (deg). */
    fun moon(epochMs: Long, lat: Double, lon: Double): Pair<Pos, Double> {
        val jd = jd(epochMs)
        val d = jd - 2451545.0
        val l = norm360(218.316 + 13.176396 * d)
        val m = norm360(134.963 + 13.064993 * d) * D2R
        val f = norm360(93.272 + 13.229350 * d) * D2R
        val lonE = (l + 6.289 * sin(m)) * D2R
        val latE = 5.128 * sin(f) * D2R
        val eps = 23.439 * D2R
        val ra = atan2(sin(lonE) * cos(eps) - tan(latE) * sin(eps), cos(lonE))
        val dec = asin(sin(latE) * cos(eps) + cos(latE) * sin(eps) * sin(lonE))
        return toHorizontal(ra, dec, jd, lat, lon) to norm360(lonE / D2R)
    }

    /** Maps a horizontal position to the renderer's world space (camera looks south: +x west, +y up, +z south). */
    fun toWorld(p: Pos, out: FloatArray, i: Int) {
        val east = sin(p.azimuth) * cos(p.altitude)
        val north = cos(p.azimuth) * cos(p.altitude)
        out[i] = (-east).toFloat()
        out[i + 1] = sin(p.altitude).toFloat()
        out[i + 2] = (-north).toFloat()
    }
}
