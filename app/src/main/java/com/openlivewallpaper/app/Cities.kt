package com.openlivewallpaper.app

data class City(val name: String, val lat: Double, val lon: Double, val zone: String)

object Cities {
    val list = listOf(
        City("Montréal", 45.5017, -73.5673, "America/Toronto"),
        City("Québec", 46.8139, -71.2080, "America/Toronto"),
        City("Ottawa", 45.4215, -75.6972, "America/Toronto"),
        City("Toronto", 43.6532, -79.3832, "America/Toronto"),
        City("Sherbrooke", 45.4042, -71.8929, "America/Toronto"),
        City("Saguenay", 48.4280, -71.0686, "America/Toronto"),
        City("Gatineau", 45.4765, -75.7013, "America/Toronto"),
        City("Halifax", 44.6488, -63.5752, "America/Halifax"),
        City("Winnipeg", 49.8951, -97.1384, "America/Winnipeg"),
        City("Calgary", 51.0447, -114.0719, "America/Edmonton"),
        City("Vancouver", 49.2827, -123.1207, "America/Vancouver"),
        City("Paris", 48.8566, 2.3522, "Europe/Paris"),
        City("New York", 40.7128, -74.0060, "America/New_York"),
        City("Reykjavik", 64.1466, -21.9426, "Atlantic/Reykjavik"),
    )
    fun byName(n: String?) = list.firstOrNull { it.name == n } ?: list[0]
}
