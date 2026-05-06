<script setup lang="ts">
import type { NvtVehicle, NvtRouteMap, NvtStopGroup, Network, NvtLine } from '~/types/nvt'
import { fetchVehicles, fetchLineRoute } from '~/composables/useNvt'
import { networkMeta } from '~/types/nvt'

const props = defineProps<{
  network: Network
  line: NvtLine | null
  stops: NvtStopGroup[]
  selectedStop: NvtStopGroup | null
}>()
const emit = defineEmits<{ stopSelect: [stop: NvtStopGroup] }>()

const { $mapkit } = useNuxtApp() as unknown as { $mapkit: { ready: () => Promise<any> } }
const { public: cfg } = useRuntimeConfig()

const mapEl = ref<HTMLDivElement | null>(null)
let map: any = null
let mk: any = null

const status = ref<'init' | 'ready' | 'error'>('init')
const errorMsg = ref('')
const vehicleCount = ref(0)
const lastUpdate = ref(0)
const tintColor = ref<string>('#7ad7f0')

let routeOverlays: any[] = []
let vehicleAnnotations: any[] = []
let selectedStopAnnotation: any = null
let routeBounds: { minLat: number; maxLat: number; minLon: number; maxLon: number } | null = null

let vehicleTimer: ReturnType<typeof setInterval> | null = null
let routeToken = 0
let vehicleToken = 0

function lineColor(): string {
  const c = props.line?.colorBg
  if (!c) return '#7ad7f0'
  return c.startsWith('#') ? c : `#${c}`
}

function isFiniteCoord(lat?: number | null, lon?: number | null): boolean {
  return (
    typeof lat === 'number' && typeof lon === 'number' &&
    Number.isFinite(lat) && Number.isFinite(lon) &&
    !(lat === 0 && lon === 0) &&
    Math.abs(lat) <= 90 && Math.abs(lon) <= 180
  )
}

onMounted(async () => {
  try {
    mk = await $mapkit.ready()
  } catch (e: any) {
    errorMsg.value = e?.message || 'MapKit init failed'
    status.value = 'error'
    return
  }

  if (!mapEl.value) return

  const meta = networkMeta(props.network)
  const initialRegion = new mk.CoordinateRegion(
    new mk.Coordinate(meta.view[0], meta.view[1]),
    new mk.CoordinateSpan(meta.view[2], meta.view[3])
  )

  map = new mk.Map(mapEl.value, {
    region: initialRegion,
    showsCompass: mk.FeatureVisibility.Adaptive,
    showsScale: mk.FeatureVisibility.Adaptive,
    showsZoomControl: true,
    showsMapTypeControl: false,
    colorScheme: mk.Map.ColorSchemes.Dark,
    isRotationEnabled: false,
    showsUserLocationControl: false,
    padding: new mk.Padding({ top: 60, right: 24, bottom: 24, left: 24 })
  })

  status.value = 'ready'
  tintColor.value = lineColor()

  if (props.line) await loadRouteAndFit()
  refreshSelectedStop()
  await refreshVehicles()

  const ms = Number(cfg.refreshIntervalMs) || 15000
  vehicleTimer = setInterval(refreshVehicles, ms)
})

onBeforeUnmount(() => {
  if (vehicleTimer) clearInterval(vehicleTimer)
  if (selectedStopAnnotation && map) {
    try { map.removeAnnotation(selectedStopAnnotation) } catch {}
    selectedStopAnnotation = null
  }
  if (map) { try { map.destroy() } catch {} }
})

watch(() => props.network, async () => {
  if (!map) return
  routeBounds = null
  vehicleCount.value = 0
  clearOverlays(routeOverlays)
  clearAnnotations(vehicleAnnotations)
  refreshSelectedStop()

  const meta = networkMeta(props.network)
  map.setRegionAnimated(
    new mk.CoordinateRegion(
      new mk.Coordinate(meta.view[0], meta.view[1]),
      new mk.CoordinateSpan(meta.view[2], meta.view[3])
    ),
    true
  )
  if (props.line) await loadRouteAndFit()
  refreshSelectedStop()
  await refreshVehicles()
})

watch(() => props.line?.gid, async (gid) => {
  if (!map) return
  tintColor.value = lineColor()
  if (gid) {
    await loadRouteAndFit()
  } else {
    clearOverlays(routeOverlays)
    routeBounds = null
    const meta = networkMeta(props.network)
    map.setRegionAnimated(
      new mk.CoordinateRegion(
        new mk.Coordinate(meta.view[0], meta.view[1]),
        new mk.CoordinateSpan(meta.view[2], meta.view[3])
      ),
      true
    )
  }
  refreshSelectedStop()
  await refreshVehicles()
})

watch(() => props.selectedStop, (s) => {
  refreshSelectedStop()
  if (!map || !s || !isFiniteCoord(s.lat, s.lon)) return
  map.setCenterAnimated(new mk.Coordinate(s.lat!, s.lon!), true)
})

function clearOverlays(list: any[]) {
  if (!map || !list.length) return
  for (const o of list) { try { map.removeOverlay(o) } catch {} }
  list.length = 0
}
function clearAnnotations(list: any[]) {
  if (!map || !list.length) return
  try { map.removeAnnotations(list) } catch {}
  list.length = 0
}

function lineStyle(rgb: string, width = 5) {
  return new mk.Style({
    strokeColor: rgb,
    lineWidth: width,
    strokeOpacity: 0.92,
    lineCap: 'round',
    lineJoin: 'round'
  })
}

async function loadRouteAndFit() {
  if (!map || !props.line) return
  const myToken = ++routeToken
  clearOverlays(routeOverlays)
  routeBounds = null
  try {
    const route: NvtRouteMap = await fetchLineRoute(props.network, props.line.gid)
    if (myToken !== routeToken) return
    if (!route?.paths?.length) return

    const color = lineColor()
    // Backend kinds: 1=BOUNDARY 2=ALLER 3=RETOUR 4=ROAD 5=RAIL 6=WATER.
    const ROUTE_KINDS = new Set([2, 3])

    let minLat = Infinity, maxLat = -Infinity
    let minLon = Infinity, maxLon = -Infinity
    let pointCount = 0

    for (const p of route.paths) {
      if (!p.points?.length) continue
      if (!ROUTE_KINDS.has(p.kind)) continue
      const coords: any[] = []
      for (const pt of p.points) {
        if (!isFiniteCoord(pt.lat, pt.lon)) continue
        coords.push(new mk.Coordinate(pt.lat, pt.lon))
        if (pt.lat < minLat) minLat = pt.lat
        if (pt.lat > maxLat) maxLat = pt.lat
        if (pt.lon < minLon) minLon = pt.lon
        if (pt.lon > maxLon) maxLon = pt.lon
        pointCount++
      }
      if (coords.length < 2) continue
      const poly = new mk.PolylineOverlay(coords, {
        style: lineStyle(color, p.kind === 3 ? 4 : 5)
      })
      map.addOverlay(poly)
      routeOverlays.push(poly)
    }

    if (pointCount >= 2 && maxLat > minLat && maxLon > minLon) {
      routeBounds = { minLat, maxLat, minLon, maxLon }
      const center = new mk.Coordinate((minLat + maxLat) / 2, (minLon + maxLon) / 2)
      const span = new mk.CoordinateSpan(
        Math.max(0.01, (maxLat - minLat) * 1.30),
        Math.max(0.01, (maxLon - minLon) * 1.30)
      )
      map.setRegionAnimated(new mk.CoordinateRegion(center, span), true)
    }
  } catch {
    /* not all networks expose route geometry */
  }
}

function refreshSelectedStop() {
  if (!map) return
  if (selectedStopAnnotation) {
    try { map.removeAnnotation(selectedStopAnnotation) } catch {}
    selectedStopAnnotation = null
  }
  const s = props.selectedStop
  if (!s || !isFiniteCoord(s.lat, s.lon)) return

  const coord = new mk.Coordinate(s.lat!, s.lon!)
  const color = lineColor()
  selectedStopAnnotation = new mk.Annotation(coord, () => {
    const wrapper = document.createElement('div')
    wrapper.className = 'nvt-stop'
    wrapper.style.setProperty('--vc', color)
    wrapper.innerHTML = `
      <span class="nvt-stop-pulse"></span>
      <span class="nvt-stop-ring"></span>
      <span class="nvt-stop-core"></span>
    `
    return wrapper
  }, {
    title: s.libelle,
    subtitle: s.groupe && s.groupe.toLowerCase() !== s.libelle.toLowerCase() ? s.groupe : '',
    displayPriority: 1100
  })
  map.addAnnotation(selectedStopAnnotation)
}

async function refreshVehicles() {
  if (!map) return
  if (!props.line) {
    clearAnnotations(vehicleAnnotations)
    vehicleCount.value = 0
    return
  }
  const myToken = ++vehicleToken
  try {
    const res = await fetchVehicles(props.network, props.line.gid)
    if (myToken !== vehicleToken) return

    let next = (res.items || []).filter(v => isFiniteCoord(v.lat, v.lon))

    // Drop vehicles whose GPS is far outside the route corridor —
    // backend occasionally returns depot or stale-trip coords.
    if (routeBounds) {
      const padLat = Math.max(0.01, (routeBounds.maxLat - routeBounds.minLat) * 0.30)
      const padLon = Math.max(0.01, (routeBounds.maxLon - routeBounds.minLon) * 0.30)
      next = next.filter(v =>
        v.lat >= routeBounds!.minLat - padLat &&
        v.lat <= routeBounds!.maxLat + padLat &&
        v.lon >= routeBounds!.minLon - padLon &&
        v.lon <= routeBounds!.maxLon + padLon
      )
    }

    clearAnnotations(vehicleAnnotations)

    const baseColor = lineColor()
    const lineLabel = (props.line.code || '').slice(0, 3).toUpperCase() || '·'

    for (const v of next) {
      const tone = v.tone || 'stable'
      const color = tone === 'critical' ? '#ef5a5a'
                  : tone === 'warning'  ? '#e0a464'
                  : baseColor
      const coord = new mk.Coordinate(v.lat, v.lon)
      const ann = new mk.MarkerAnnotation(coord, {
        color,
        glyphColor: '#0a0e1a',
        glyphText: v.delayLabel?.trim() || lineLabel,
        title: `${props.line!.code || ''} → ${v.terminus || v.nextStopName || ''}`.trim(),
        subtitle: vehicleSubtitle(v),
        selected: false,
        animates: true,
        displayPriority: 1000,
        collisionMode: mk.Annotation.CollisionMode.Circle
      })
      vehicleAnnotations.push(ann)
    }
    map.addAnnotations(vehicleAnnotations)
    vehicleCount.value = next.length
    lastUpdate.value = Date.now()
  } catch {
    vehicleCount.value = 0
  }
}

function vehicleSubtitle(v: NvtVehicle) {
  const parts: string[] = []
  if (v.vitesse) parts.push(`${v.vitesse} km/h`)
  if (v.delayLabel) parts.push(v.delayLabel)
  if (v.nextStopName) parts.push(`→ ${v.nextStopName}`)
  return parts.join(' • ')
}
</script>

<template>
  <div class="map-wrap">
    <div ref="mapEl" class="map" :class="{ ready: status === 'ready' }"></div>

    <!-- Line-color ambient tint — recolors the entire map (water, land, roads)
         while preserving its luminance via mix-blend-mode: color. -->
    <div
      v-if="status === 'ready' && line"
      class="tint-overlay"
      :style="{ '--tint': tintColor }"
      aria-hidden="true"
    ></div>

    <div v-if="status === 'ready'" class="vignette" aria-hidden="true"></div>

    <div v-if="status === 'init'" class="overlay glass">
      <span class="dot pulse amber"></span>
      <span class="muted">Loading map</span>
    </div>

    <div v-else-if="status === 'error'" class="overlay glass error">
      <span class="dot danger"></span>
      <div class="ovl-text">
        <span class="ovl-title">Map unavailable</span>
        <span class="muted">{{ errorMsg }}</span>
        <span class="ghost">Set MAPKIT_JS_TOKEN in .env.</span>
      </div>
    </div>

    <div v-if="status === 'ready'" class="hud glass">
      <span class="dot pulse amber"></span>
      <span class="hud-text">Live</span>
      <span class="hud-sep">·</span>
      <span class="hud-text mono tabular">{{ vehicleCount }} vehicles</span>
      <template v-if="line">
        <span class="hud-sep">·</span>
        <span class="hud-line" :style="{ color: tintColor }">{{ line.code }}</span>
      </template>
    </div>
  </div>
</template>

<style>
.map-wrap {
  position: relative;
  width: 100%;
  height: 100%;
  border-radius: var(--r-lg);
  overflow: hidden;
  background: var(--bg-tide);
  box-shadow: var(--glass-shadow), var(--glass-edge-inner);
}

.map {
  width: 100%;
  height: 100%;
  border-radius: var(--r-lg);
  overflow: hidden;
  opacity: 0;
  transition: opacity 500ms ease;
}
.map.ready { opacity: 1; }

/* Whole-map color shift driven by selected line color.
   `mix-blend-mode: color` keeps the map's luminance (so streets and labels
   stay readable) but replaces its hue + saturation with the line color.
   A second softer wash on top adds organic edge falloff. */
.tint-overlay {
  position: absolute;
  inset: 0;
  pointer-events: none;
  z-index: 1;
  background: var(--tint);
  opacity: 0.45;
  mix-blend-mode: color;
  transition: opacity 600ms ease, background 600ms ease;
}
.tint-overlay::after {
  content: '';
  position: absolute;
  inset: 0;
  background:
    radial-gradient(120% 80% at 50% 110%, var(--tint), transparent 55%),
    radial-gradient(70% 50% at 0% 0%, var(--tint), transparent 60%);
  opacity: 0.30;
  mix-blend-mode: soft-light;
}

.vignette {
  position: absolute;
  inset: 0;
  pointer-events: none;
  z-index: 1;
  background: radial-gradient(120% 100% at 50% 50%, transparent 55%, rgba(4, 6, 13, 0.55) 100%);
}

.overlay {
  position: absolute;
  top: 16px; left: 16px;
  padding: 10px 14px;
  display: inline-flex;
  align-items: center;
  gap: 10px;
  font-size: 13px;
  z-index: 3;
}
.overlay.error { max-width: 320px; align-items: flex-start; }
.ovl-text { display: flex; flex-direction: column; gap: 2px; }
.ovl-title { font-weight: 600; color: var(--rouge); font-size: 13px; }

.hud {
  position: absolute;
  top: 16px;
  right: 16px;
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 7px 14px;
  font-size: 12px;
  z-index: 3;
}
.hud-text { color: var(--ink-soft); }
.hud-sep { color: var(--ink-faint); }
.hud-line {
  font-family: var(--font-mono);
  font-weight: 600;
}

/* ─── Selected-stop annotation ─────────────────────────── */
.nvt-stop {
  --vc: #ffb84d;
  position: absolute;
  width: 0;
  height: 0;
  pointer-events: none;
}
.nvt-stop-pulse,
.nvt-stop-ring,
.nvt-stop-core {
  position: absolute;
  top: 0; left: 0;
  border-radius: 50%;
  pointer-events: none;
}
.nvt-stop-pulse {
  width: 28px; height: 28px;
  margin: -14px 0 0 -14px;
  background: var(--vc);
  opacity: 0.30;
  animation: nvt-stop-pulse 2.4s ease-out infinite;
}
.nvt-stop-ring {
  width: 18px; height: 18px;
  margin: -9px 0 0 -9px;
  background: transparent;
  border: 2px solid var(--vc);
  box-shadow: 0 0 14px var(--vc);
}
.nvt-stop-core {
  width: 8px; height: 8px;
  margin: -4px 0 0 -4px;
  background: var(--vc);
  border: 1.5px solid rgba(4, 6, 13, 0.95);
  box-shadow: 0 0 8px var(--vc);
}
@keyframes nvt-stop-pulse {
  0%   { transform: scale(0.5); opacity: 0.45; }
  80%  { transform: scale(1.8); opacity: 0; }
  100% { transform: scale(1.8); opacity: 0; }
}
</style>
