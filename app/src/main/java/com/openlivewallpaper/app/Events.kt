package com.openlivewallpaper.app

import java.time.DayOfWeek
import java.time.LocalDate
import java.time.temporal.TemporalAdjusters

/** Holiday / seasonal event bit flags (keep in sync with common.h). */
object Events {
    const val CHRISTMAS = 1
    const val NEWYEAR = 2
    const val QUEBEC = 4
    const val CANADA = 8
    const val HALLOWEEN = 16
    const val EASTER = 32
    const val THANKS = 64
    const val VALENTINE = 128
    const val CARNAVAL = 256
    const val STPATRICK = 512
    const val SUGAR = 1024
    const val FIREWORKS = 2048

    val all = listOf(
        "Christmas" to CHRISTMAS, "New Year" to NEWYEAR, "Fête nationale" to QUEBEC, "Canada Day" to CANADA,
        "Halloween" to HALLOWEEN, "Easter" to EASTER, "Thanksgiving" to THANKS, "Valentine" to VALENTINE,
        "Carnaval" to CARNAVAL, "St Patrick" to STPATRICK, "Sugar shack" to SUGAR,
    )

    fun easter(year: Int): LocalDate {
        val a = year % 19; val b = year / 100; val c = year % 100
        val d = b / 4; val e = b % 4; val f = (b + 8) / 25; val g = (b - f + 1) / 3
        val h = (19 * a + b - d - g + 15) % 30
        val i = c / 4; val k = c % 4
        val l = (32 + 2 * e + 2 * i - h - k) % 7
        val m = (a + 11 * h + 22 * l) / 451
        val month = (h + l - 7 * m + 114) / 31
        val day = (h + l - 7 * m + 114) % 31 + 1
        return LocalDate.of(year, month, day)
    }

    /** Canadian Thanksgiving: second Monday of October. */
    fun thanksgiving(year: Int): LocalDate =
        LocalDate.of(year, 10, 1).with(TemporalAdjusters.dayOfWeekInMonth(2, DayOfWeek.MONDAY))

    private fun between(d: LocalDate, from: LocalDate, to: LocalDate) = !d.isBefore(from) && !d.isAfter(to)

    fun forDate(d: LocalDate): Int {
        val y = d.year
        var m = 0
        fun md(mo: Int, day: Int) = LocalDate.of(y, mo, day)
        if (between(d, md(12, 1), md(12, 26))) m = m or CHRISTMAS
        if (between(d, md(12, 27), md(12, 31)) || between(d, md(1, 1), md(1, 2))) m = m or NEWYEAR
        if (d == md(12, 31) || d == md(1, 1)) m = m or FIREWORKS
        if (between(d, md(2, 8), md(2, 14))) m = m or VALENTINE
        if (between(d, md(1, 30), md(2, 15))) m = m or CARNAVAL
        if (between(d, md(3, 13), md(3, 17))) m = m or STPATRICK
        if (between(d, md(3, 8), md(4, 20))) m = m or SUGAR
        val e = easter(y)
        if (between(d, e.minusDays(4), e.plusDays(1))) m = m or EASTER
        if (between(d, md(6, 20), md(6, 24))) m = m or QUEBEC
        if (d == md(6, 23) || d == md(6, 24)) m = m or FIREWORKS
        if (between(d, md(6, 28), md(7, 1))) m = m or CANADA
        if (d == md(6, 30) || d == md(7, 1)) m = m or FIREWORKS
        if (between(d, md(10, 22), md(10, 31))) m = m or HALLOWEEN
        val t = thanksgiving(y)
        if (between(d, t.minusDays(3), t.plusDays(1))) m = m or THANKS
        return m
    }
}
