package com.openlivewallpaper.app

import android.app.WallpaperManager
import android.content.ComponentName
import android.content.Intent
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat

class MainActivity : ComponentActivity() {
    private lateinit var prefs: Prefs
    private lateinit var driver: SceneDriver
    private lateinit var weather: WeatherRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        prefs = Prefs(this).also { it.allowPreview = true }
        weather = WeatherRepository(this)
        driver = SceneDriver(this, prefs, weather)
        setContent { MaterialTheme(colorScheme = darkColorScheme()) { App() } }
    }

    override fun onStart() { super.onStart(); driver.setVisible(true) }
    override fun onStop() { driver.setVisible(false); super.onStop() }
    override fun onDestroy() { driver.destroy(); super.onDestroy() }

    @Composable
    private fun App() {
        Box(Modifier.fillMaxSize().background(Color.Black)) {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { ctx ->
                    SurfaceView(ctx).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(h: SurfaceHolder) {}
                            override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, hh: Int) {
                                driver.setSurface(h.surface, w, hh)
                            }
                            override fun surfaceDestroyed(h: SurfaceHolder) { driver.setSurface(null, 0, 0) }
                        })
                    }
                },
            )
            ControlPanel(Modifier.align(Alignment.BottomCenter))
        }
    }

    @OptIn(ExperimentalLayoutApi::class)
    @Composable
    private fun ControlPanel(modifier: Modifier) {
        val ctx = LocalContext.current
        var expanded by remember { mutableStateOf(false) }
        var tab by remember { mutableIntStateOf(0) }
        // mirrored prefs state
        var pvOn by remember { mutableStateOf(prefs.previewOn) }
        var season by remember { mutableIntStateOf(prefs.pvSeason) }
        var hour by remember { mutableFloatStateOf(prefs.pvHour) }
        var speed by remember { mutableIntStateOf(prefs.pvSpeed) }
        var wx by remember { mutableIntStateOf(prefs.pvWeather) }
        var ev by remember { mutableIntStateOf(prefs.pvEvent) }
        var pvAurora by remember { mutableStateOf(prefs.pvAurora) }
        var city by remember { mutableStateOf(prefs.city) }
        var quality by remember { mutableIntStateOf(prefs.quality) }
        var fps60 by remember { mutableStateOf(prefs.fps60) }
        var eventsOn by remember { mutableStateOf(prefs.eventsOn) }
        var liveWx by remember { mutableStateOf(prefs.liveWeather) }
        var scrollFx by remember { mutableStateOf(prefs.scrollEffect) }
        var aurora by remember { mutableIntStateOf(prefs.auroraMode) }

        Column(
            modifier.fillMaxWidth().navigationBarsPadding().padding(12.dp)
                .background(Color(0xB0101418), RoundedCornerShape(24.dp)).padding(14.dp),
        ) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                Button(onClick = {
                    val i = Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER)
                        .putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT, ComponentName(ctx, LiveWallpaperService::class.java))
                    startActivity(i)
                }, modifier = Modifier.weight(1f)) { Text("Set as wallpaper") }
                FilledTonalButton(onClick = { expanded = !expanded }) { Text(if (expanded) "Hide" else "Preview & settings") }
            }
            if (expanded) {
                Spacer(Modifier.height(8.dp))
                TabRow(selectedTabIndex = tab, containerColor = Color.Transparent) {
                    Tab(selected = tab == 0, onClick = { tab = 0 }, text = { Text("Preview") })
                    Tab(selected = tab == 1, onClick = { tab = 1 }, text = { Text("Settings") })
                }
                Column(Modifier.heightIn(max = 380.dp).verticalScroll(rememberScrollState()).padding(top = 8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    if (tab == 0) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("Preview mode (in this app only)", Modifier.weight(1f))
                            Switch(checked = pvOn, onCheckedChange = { pvOn = it; prefs.previewOn = it })
                        }
                        Label("Season")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(-1 to "Live", 0 to "Winter", 1 to "Spring", 2 to "Summer", 3 to "Autumn").forEach { (k, n) ->
                                FilterChip(season == k, { season = k; prefs.pvSeason = k; if (!pvOn) { pvOn = true; prefs.previewOn = true } }, { Text(n) })
                            }
                        }
                        Label("Time of day: " + if (hour < 0) "live" else "%02d:%02d".format(hour.toInt(), ((hour - hour.toInt()) * 60).toInt()))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            FilterChip(hour < 0, { hour = -1f; prefs.pvHour = -1f }, { Text("Live") })
                            Slider(
                                value = if (hour < 0) 12f else hour, onValueChange = { hour = it; prefs.pvHour = it; if (!pvOn) { pvOn = true; prefs.previewOn = true } },
                                valueRange = 0f..23.99f, modifier = Modifier.weight(1f).padding(start = 10.dp),
                            )
                        }
                        Label("Time-lapse")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(1 to "1×", 60 to "60×", 600 to "600×", 3600 to "3600×").forEach { (k, n) ->
                                FilterChip(speed == k, { speed = k; prefs.pvSpeed = k; if (k > 1) { hour = -1f; prefs.pvHour = -1f; pvOn = true; prefs.previewOn = true } }, { Text(n) })
                            }
                        }
                        Label("Weather")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(0 to "Live", 1 to "Clear", 2 to "Partly", 3 to "Overcast", 4 to "Rain", 5 to "Storm", 6 to "Snow", 7 to "Blizzard", 8 to "Fog").forEach { (k, n) ->
                                FilterChip(wx == k, { wx = k; prefs.pvWeather = k; if (!pvOn) { pvOn = true; prefs.previewOn = true } }, { Text(n) })
                            }
                        }
                        Label("Event")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            FilterChip(ev == -1, { ev = -1; prefs.pvEvent = -1 }, { Text("By date") })
                            FilterChip(ev == 0, { ev = 0; prefs.pvEvent = 0; if (!pvOn) { pvOn = true; prefs.previewOn = true } }, { Text("None") })
                            Events.all.forEach { (n, bit) ->
                                FilterChip(ev == bit, { ev = bit; prefs.pvEvent = bit; if (!pvOn) { pvOn = true; prefs.previewOn = true } }, { Text(n) })
                            }
                        }
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("Force aurora", Modifier.weight(1f))
                            Switch(checked = pvAurora, onCheckedChange = { pvAurora = it; prefs.pvAurora = it; if (!pvOn) { pvOn = true; prefs.previewOn = true } })
                        }
                        TextButton(onClick = {
                            prefs.resetPreview(); pvOn = false; season = -1; hour = -1f; speed = 1; wx = 0; ev = -1; pvAurora = false
                        }) { Text("Back to live") }
                    } else {
                        Label("City (time, sun, moon, weather)")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            Cities.list.forEach { c -> FilterChip(city == c.name, { city = c.name; prefs.city = c.name }, { Text(c.name) }) }
                        }
                        Label("Quality")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(0 to "Auto", 1 to "Low", 2 to "Medium", 3 to "High").forEach { (k, n) ->
                                FilterChip(quality == k, { quality = k; prefs.quality = k }, { Text(n) })
                            }
                        }
                        Text("Quality changes apply the next time the wallpaper starts.", style = MaterialTheme.typography.bodySmall)
                        SwitchRow("60 fps (30 fps saves battery)", fps60) { fps60 = it; prefs.fps60 = it }
                        SwitchRow("Live weather (Open-Meteo)", liveWx) { liveWx = it; prefs.liveWeather = it }
                        SwitchRow("Holiday events", eventsOn) { eventsOn = it; prefs.eventsOn = it }
                        SwitchRow("Home screen scroll effect", scrollFx) { scrollFx = it; prefs.scrollEffect = it }
                        Label("Aurora borealis")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(0 to "Occasional", 1 to "Every clear night", 2 to "Never").forEach { (k, n) ->
                                FilterChip(aurora == k, { aurora = k; prefs.auroraMode = k }, { Text(n) })
                            }
                        }
                    }
                }
            }
        }
    }

    @Composable private fun Label(t: String) = Text(t, style = MaterialTheme.typography.labelLarge, color = Color(0xFFB8C7DD))

    @Composable
    private fun SwitchRow(t: String, v: Boolean, onChange: (Boolean) -> Unit) =
        Row(verticalAlignment = Alignment.CenterVertically) { Text(t, Modifier.weight(1f)); Switch(checked = v, onCheckedChange = onChange) }
}
