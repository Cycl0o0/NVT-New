<script setup lang="ts">
import type { Network, NvtLine, NvtStopGroup } from '~/types/nvt'
import { useNvtLines, useNvtAlerts, useNvtStopGroups } from '~/composables/useNvt'

const { public: cfg } = useRuntimeConfig()

type Tab = 'lines' | 'live' | 'alerts'
type SheetSize = 'peek' | 'medium' | 'full'

const network = ref<Network>((cfg.defaultNetwork as Network) || 'bdx')
const selectedLine = ref<NvtLine | null>(null)
const selectedStop = ref<NvtStopGroup | null>(null)
const tab = ref<Tab>('lines')
const sheetSize = ref<SheetSize>('medium')
const panelCollapsed = ref(false)

const selectedLineGid = computed(() => selectedLine.value?.gid ?? null)

const { data: linesData, pending: linesPending, error: linesError, refresh: refreshLines } = useNvtLines(network)
const { data: alertsData } = useNvtAlerts(network)
const { data: stopsData, pending: stopsPending } = useNvtStopGroups(network, selectedLineGid)

const lines  = computed(() => linesData.value?.items ?? [])
const alerts = computed(() => alertsData.value?.items ?? [])
const stops  = computed(() => stopsData.value?.items ?? [])
const lineStats  = computed(() => linesData.value?.stats ?? {})
const alertStats = computed(() => alertsData.value?.stats ?? {})

watch(network, () => {
  selectedLine.value = null
  selectedStop.value = null
})
watch(selectedLine, () => { selectedStop.value = null })

function pickLine(line: NvtLine) {
  const same = selectedLine.value?.gid === line.gid
  selectedLine.value = same ? null : line
  if (!same) {
    tab.value = 'live'
    if (sheetSize.value === 'peek') sheetSize.value = 'medium'
  }
}
function pickStop(stop: NvtStopGroup) {
  const same = selectedStop.value?.key === stop.key
  selectedStop.value = same ? null : stop
  if (!same) {
    tab.value = 'live'
    if (sheetSize.value === 'peek') sheetSize.value = 'medium'
  }
}

const backendOk = ref<boolean | null>(null)
const mounted = ref(false)
const winWidth = ref(1280)
const now = ref<Date>(new Date())

function updateWinWidth() {
  if (typeof window !== 'undefined') winWidth.value = window.innerWidth
}

let clockTimer: ReturnType<typeof setInterval> | null = null

onMounted(async () => {
  mounted.value = true
  updateWinWidth()
  window.addEventListener('resize', updateWinWidth, { passive: true })
  clockTimer = setInterval(() => { now.value = new Date() }, 1000)
  try {
    const h = await $fetch<{ ok: boolean }>('/api/health')
    backendOk.value = h.ok
  } catch {
    backendOk.value = false
  }
})
onBeforeUnmount(() => {
  if (typeof window !== 'undefined') window.removeEventListener('resize', updateWinWidth)
  if (clockTimer) clearInterval(clockTimer)
})

const isDesktop = computed(() => mounted.value && winWidth.value >= 1024)
const refreshDisabled = computed(() => mounted.value && linesPending.value)
const criticalAlertCount = computed(() => alerts.value.filter((a) => /3|crit/i.test(a.severite)).length)

const wallclock = computed(() => {
  const d = now.value
  const hh = String(d.getHours()).padStart(2, '0')
  const mm = String(d.getMinutes()).padStart(2, '0')
  return `${hh}:${mm}`
})

const networkCity = computed(() => {
  const map: Record<Network, string> = {
    bdx: 'Bordeaux', tls: 'Toulouse', idfm: 'Paris', sncf: 'France', star: 'Rennes'
  }
  return map[network.value]
})

function cycleSheetSize() {
  const order: SheetSize[] = ['peek', 'medium', 'full']
  const i = order.indexOf(sheetSize.value)
  sheetSize.value = order[(i + 1) % order.length]
}
</script>

<template>
  <div class="shell" :data-tab="tab">

    <!-- ───── MAP: fills the viewport ───── -->
    <div class="map-layer">
      <ClientOnly>
        <NvtMap
          :network="network"
          :line="selectedLine"
          :stops="stops"
          :selected-stop="selectedStop"
          @stop-select="pickStop"
        />
      </ClientOnly>
    </div>

    <!-- ───── TOPBAR: floating pill at top ───── -->
    <header class="topbar glass-strong">
      <div class="brand">
        <span class="brand-mark">NVT</span>
        <span class="brand-divider"></span>
        <span class="brand-city">{{ networkCity }}</span>
      </div>

      <div class="topbar-mid">
        <NetworkSwitcher v-model="network" :compact="true" />
      </div>

      <div class="topbar-right">
        <span class="clock mono tabular hide-sm">{{ wallclock }}</span>
        <span
          class="status"
          :title="backendOk === false ? 'Backend offline' : 'Backend online'"
        >
          <span class="dot" :class="backendOk === null ? 'warn' : (backendOk ? 'ok' : 'danger')"></span>
        </span>
        <button class="btn ghost icon-btn" :disabled="refreshDisabled" @click="refreshLines()" aria-label="Refresh">
          <span aria-hidden="true">↻</span>
        </button>
      </div>
    </header>

    <!-- ───── MAIN PANEL: floating side panel (desktop) / bottom sheet (mobile) ───── -->
    <aside
      class="panel glass-strong"
      :class="[`sheet-${sheetSize}`, { collapsed: panelCollapsed && isDesktop }]"
    >
      <!-- Mobile drag handle -->
      <button
        class="panel-handle only-sheet"
        @click="cycleSheetSize"
        aria-label="Toggle panel"
      >
        <span class="handle-bar"></span>
      </button>

      <!-- Desktop collapse button -->
      <button
        class="panel-collapse only-desktop"
        @click="panelCollapsed = !panelCollapsed"
        :aria-label="panelCollapsed ? 'Expand panel' : 'Collapse panel'"
      >
        <span aria-hidden="true">{{ panelCollapsed ? '›' : '‹' }}</span>
      </button>

      <!-- Tabs -->
      <nav class="tabs" role="tablist">
        <button
          class="tab"
          :class="{ active: tab === 'lines' }"
          role="tab"
          @click="tab = 'lines'"
        >
          <span class="tab-lbl">Lines</span>
          <span class="tab-num mono tabular">{{ lineStats.total ?? lines.length }}</span>
        </button>
        <button
          class="tab"
          :class="{ active: tab === 'live' }"
          role="tab"
          @click="tab = 'live'"
        >
          <span class="tab-lbl">Live</span>
          <LineBadge v-if="selectedLine" :line="selectedLine" size="sm" />
        </button>
        <button
          class="tab"
          :class="{ active: tab === 'alerts' }"
          role="tab"
          @click="tab = 'alerts'"
        >
          <span class="tab-lbl">Alerts</span>
          <span
            v-if="alertStats.total"
            class="tab-num mono tabular"
            :class="{ 'tab-num-rouge': criticalAlertCount > 0 }"
          >{{ alertStats.total }}</span>
        </button>
      </nav>

      <!-- Tab content -->
      <div class="tab-body">
        <div v-show="tab === 'lines'" class="tab-pane">
          <LineList
            :lines="lines"
            :selected-gid="selectedLineGid"
            :loading="linesPending"
            @select="pickLine"
          />
        </div>

        <div v-show="tab === 'live'" class="tab-pane live-pane">
          <div v-if="!selectedLine" class="placeholder">
            <span class="muted">Pick a line</span>
            <span class="ghost">Select a line in the Lines tab to see live stops, departures and vehicles.</span>
            <button class="btn amber" @click="tab = 'lines'">Browse lines</button>
          </div>

          <template v-else>
            <header class="line-head">
              <LineBadge :line="selectedLine" size="lg" />
              <div class="line-head-text">
                <span class="line-head-name">{{ selectedLine.libelle }}</span>
                <span class="line-head-meta mono">{{ selectedLine.vehicule }} · {{ stops.length }} stops</span>
              </div>
              <button
                class="btn ghost icon-btn"
                @click="selectedLine = null"
                aria-label="Clear selection"
                title="Clear selection"
              >×</button>
            </header>

            <div class="live-sections">
              <div class="live-section live-stops">
                <StopList
                  :stops="stops"
                  :selected-key="selectedStop?.key ?? null"
                  :loading="stopsPending"
                  @select="pickStop"
                />
              </div>
              <div class="live-section live-deps">
                <NextPassages
                  :network="network"
                  :stop="selectedStop"
                  :line-gid="selectedLineGid"
                  :line-code="selectedLine?.code ?? null"
                />
              </div>
            </div>
          </template>
        </div>

        <div v-show="tab === 'alerts'" class="tab-pane">
          <AlertsPanel :alerts="alerts" :selected-line-gid="selectedLineGid" />
        </div>
      </div>
    </aside>

    <!-- Backend offline toast -->
    <div v-if="linesError" class="toast glass">
      <span class="dot danger"></span>
      <span>Backend unreachable. Run <code class="mono">make backend-run</code>.</span>
    </div>
  </div>
</template>

<style scoped>
/* ═════════ Shell — full-bleed map under floating UI ═════════ */

.shell {
  position: relative;
  height: 100dvh;
  width: 100vw;
  overflow: hidden;
}

.map-layer {
  position: absolute;
  inset: 0;
  z-index: 0;
}

/* ═════════ Topbar — floating pill at top ═════════ */

.topbar {
  position: absolute;
  top: 12px;
  left: 12px;
  right: 12px;
  z-index: 20;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 6px 12px;
  border-radius: 999px;
  min-height: 48px;
}

.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  padding-left: 6px;
  flex-shrink: 0;
}
.brand-mark {
  font-size: 15px;
  font-weight: 700;
  letter-spacing: -0.01em;
  color: var(--ink);
}
.brand-divider {
  width: 1px;
  height: 14px;
  background: var(--rule);
}
.brand-city {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--ink-soft);
  white-space: nowrap;
}

.topbar-mid {
  flex: 1;
  display: flex;
  justify-content: center;
  min-width: 0;
  overflow-x: auto;
  scrollbar-width: none;
}
.topbar-mid::-webkit-scrollbar { display: none; }

.topbar-right {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-shrink: 0;
}
.clock {
  font-size: 13px;
  color: var(--ink-soft);
  letter-spacing: 0.04em;
  padding: 0 4px;
}
.status {
  display: inline-flex;
  align-items: center;
  width: 22px;
  justify-content: center;
}
.icon-btn { width: 30px; height: 30px; padding: 0; font-size: 14px; border-radius: 999px; }

/* ═════════ Panel — floating left (desktop) / bottom sheet (mobile) ═════════ */

.panel {
  position: absolute;
  z-index: 15;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  isolation: isolate;
}

/* Mobile / tablet — bottom sheet */
.panel {
  bottom: 0;
  left: 0;
  right: 0;
  height: 80dvh;
  max-height: 80dvh;
  border-radius: var(--r-xl) var(--r-xl) 0 0;
  transform: translateY(calc(100% - 130px));
  transition: transform 280ms cubic-bezier(0.32, 0.72, 0, 1);
}
.panel.sheet-medium { transform: translateY(50%); }
.panel.sheet-full   { transform: translateY(0); }

.panel-handle {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 100%;
  height: 22px;
  background: transparent;
  border: 0;
  cursor: pointer;
  flex-shrink: 0;
  padding: 8px 0 4px;
}
.handle-bar {
  width: 36px;
  height: 4px;
  border-radius: 999px;
  background: var(--rule-strong);
  transition: background 180ms ease;
}
.panel-handle:hover .handle-bar { background: rgba(241, 234, 214, 0.32); }

.panel-collapse { display: none; }

/* Desktop — left-edge floating panel */
@media (min-width: 1024px) {
  .panel {
    top: 72px;
    bottom: 16px;
    left: 16px;
    right: auto;
    width: 380px;
    height: auto;
    max-height: none;
    border-radius: var(--r-xl);
    transform: none;
    transition: width 280ms cubic-bezier(0.32, 0.72, 0, 1);
  }
  .panel.collapsed {
    width: 56px;
  }
  .panel.collapsed .tab-body,
  .panel.collapsed .tabs { display: none; }

  .panel-handle { display: none; }

  .panel-collapse {
    display: flex;
    align-items: center;
    justify-content: center;
    position: absolute;
    top: 16px;
    right: -14px;
    width: 28px;
    height: 28px;
    padding: 0;
    border-radius: 50%;
    background: var(--glass-3);
    border: 1px solid var(--rule);
    color: var(--ink);
    font-size: 14px;
    cursor: pointer;
    z-index: 30;
    -webkit-backdrop-filter: var(--glass-blur);
            backdrop-filter: var(--glass-blur);
    box-shadow: 0 4px 12px rgba(0,0,0,0.4);
    transition: background 160ms ease;
  }
  .panel-collapse:hover { background: var(--glass-3); border-color: var(--rule-strong); }
}

/* Wider on big screens */
@media (min-width: 1440px) {
  .panel { width: 420px; }
}

/* ═════════ Tabs ═════════ */

.tabs {
  display: flex;
  align-items: stretch;
  gap: 2px;
  padding: 4px;
  margin: 8px 12px 0;
  background: rgba(0, 0, 0, 0.30);
  border: 1px solid var(--rule);
  border-radius: 999px;
  flex-shrink: 0;
}
.tab {
  flex: 1;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 7px 10px;
  background: transparent;
  border: 0;
  border-radius: 999px;
  color: var(--ink-soft);
  font-family: var(--font-sans);
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
  transition: background 160ms ease, color 160ms ease;
  min-width: 0;
}
.tab:hover { color: var(--ink); }
.tab.active {
  background: var(--glass-3);
  color: var(--ink);
  box-shadow: inset 0 1px 0 rgba(255,255,255,0.08);
}

.tab-lbl { white-space: nowrap; }
.tab-num {
  font-size: 10px;
  color: var(--ink-faint);
  background: rgba(241, 234, 214, 0.06);
  padding: 1px 6px;
  border-radius: 999px;
  font-variant-numeric: tabular-nums;
}
.tab.active .tab-num { background: var(--glass-3); color: var(--ink-soft); }
.tab-num-rouge {
  background: rgba(239, 90, 90, 0.16) !important;
  color: var(--rouge) !important;
}

/* ═════════ Tab body ═════════ */

.tab-body {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  padding: 14px;
  overflow: hidden;
}
.tab-pane {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.placeholder {
  margin: auto;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  text-align: center;
  padding: 24px 16px;
  max-width: 280px;
  font-size: 12px;
}
.placeholder .btn { margin-top: 6px; }

/* Live tab — line head + stacked sections */
.live-pane {
  gap: 12px;
}
.line-head {
  display: flex;
  align-items: center;
  gap: 12px;
  padding-bottom: 10px;
  border-bottom: 1px solid var(--rule);
  flex-shrink: 0;
}
.line-head-text {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}
.line-head-name {
  font-size: 14px;
  font-weight: 500;
  color: var(--ink);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.line-head-meta {
  font-size: 10px;
  color: var(--ink-faint);
  letter-spacing: 0.04em;
}

.live-sections {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.live-section {
  display: flex;
  flex-direction: column;
  min-height: 0;
}
.live-stops { flex: 0 1 42%; max-height: 44%; }
.live-deps  { flex: 1 1 56%; min-height: 0; }

/* Make embedded card components stretch into available space */
.tab-pane :deep(.line-list),
.tab-pane :deep(.alerts),
.tab-pane :deep(.stop-list),
.tab-pane :deep(.passages) {
  flex: 1 1 0;
  min-height: 0;
}

/* ═════════ Toast ═════════ */

.toast {
  position: fixed;
  top: 70px;
  left: 50%;
  transform: translateX(-50%);
  padding: 9px 14px;
  display: flex;
  align-items: center;
  gap: 10px;
  z-index: 40;
  max-width: calc(100vw - 24px);
  font-size: 12px;
  border-color: rgba(239, 90, 90, 0.45);
  background: rgba(239, 90, 90, 0.10) !important;
}

/* ═════════ Visibility helpers ═════════ */

.only-sheet { display: flex; }
.only-desktop { display: none; }
.hide-sm { display: none; }

@media (min-width: 1024px) {
  .only-sheet { display: none; }
  .only-desktop { display: flex; }
}

@media (min-width: 480px) {
  .hide-sm { display: inline; }
}

/* ═════════ Topbar layout adjustments per breakpoint ═════════ */

@media (max-width: 600px) {
  .topbar {
    padding: 5px 10px;
    gap: 6px;
    min-height: 44px;
  }
  .brand-city { display: none; }
  .topbar-mid { justify-content: flex-end; }
}
</style>
