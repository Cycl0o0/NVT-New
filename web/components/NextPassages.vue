<script setup lang="ts">
import type { NvtPassage, NvtStopGroup, Network } from '~/types/nvt'
import { fetchPassages } from '~/composables/useNvt'

const props = defineProps<{
  network: Network
  stop: NvtStopGroup | null
  lineGid: number | null
  lineCode?: string | null
}>()

const { public: cfg } = useRuntimeConfig()

const passages = ref<NvtPassage[]>([])
const loading = ref(false)
const error = ref<string | null>(null)
const lastUpdate = ref<number>(0)
const now = ref<number>(Date.now())

let timer: ReturnType<typeof setInterval> | null = null
let clockTimer: ReturnType<typeof setInterval> | null = null

async function load() {
  if (!props.stop) return
  loading.value = true
  error.value = null
  try {
    const res = await fetchPassages(props.network, props.stop.key, props.lineGid)
    passages.value = res.items
    lastUpdate.value = Date.now()
  } catch (e: any) {
    error.value = e?.statusMessage || e?.message || 'Failed to load'
    passages.value = []
  } finally {
    loading.value = false
  }
}

watch(
  () => [props.stop?.key, props.lineGid, props.network],
  () => { passages.value = []; load() },
  { immediate: true }
)

onMounted(() => {
  const ms = Number(cfg.refreshIntervalMs) || 15000
  timer = setInterval(() => { if (props.stop) load() }, ms)
  clockTimer = setInterval(() => { now.value = Date.now() }, 30_000)
})
onBeforeUnmount(() => {
  if (timer) clearInterval(timer)
  if (clockTimer) clearInterval(clockTimer)
})

const filteredPassages = computed(() => {
  const all = passages.value
  if (!props.lineGid && !props.lineCode) return all
  const wantCode = (props.lineCode || '').toUpperCase()
  return all.filter((p) => {
    if (props.lineGid && p.lineId && p.lineId === props.lineGid) return true
    if (wantCode && p.lineCode && p.lineCode.toUpperCase() === wantCode) return true
    return false
  })
})

function timeUntil(p: NvtPassage): { mins: number | null, label: string } {
  if (p.waitingTime) {
    const m = p.waitingTime.match(/(\d+)\s*m(?:in)?/i)
    if (m) return { mins: parseInt(m[1], 10), label: p.waitingTime }
    return { mins: null, label: p.waitingTime }
  }
  if (p.estimated && /^\d{1,2}:\d{2}$/.test(p.estimated)) {
    const [h, mm] = p.estimated.split(':').map(Number)
    const t = new Date()
    t.setHours(h, mm, 0, 0)
    let diff = (t.getTime() - now.value) / 60000
    if (diff < -30) diff += 24 * 60
    if (diff < 0)  return { mins: 0, label: 'at platform' }
    if (diff < 1)  return { mins: 0, label: 'now' }
    return { mins: Math.round(diff), label: `${Math.round(diff)} min` }
  }
  return { mins: null, label: p.estimated || '—' }
}

function delayDelta(p: NvtPassage): { value: number, label: string } | null {
  if (!p.scheduled || !p.estimated) return null
  if (p.scheduled === p.estimated) return null
  if (!/^\d{1,2}:\d{2}$/.test(p.scheduled)) return null
  if (!/^\d{1,2}:\d{2}$/.test(p.estimated)) return null
  const [sh, sm] = p.scheduled.split(':').map(Number)
  const [eh, em] = p.estimated.split(':').map(Number)
  let diff = (eh * 60 + em) - (sh * 60 + sm)
  if (diff < -12 * 60) diff += 24 * 60
  if (diff > 12 * 60)  diff -= 24 * 60
  if (diff === 0) return null
  return { value: diff, label: (diff > 0 ? '+' : '−') + Math.abs(diff) + ' min' }
}

const ageLabel = computed(() => {
  if (!lastUpdate.value) return ''
  const s = Math.round((now.value - lastUpdate.value) / 1000)
  if (s < 5)  return 'just now'
  if (s < 60) return `${s}s ago`
  return `${Math.floor(s / 60)}m ago`
})
</script>

<template>
  <section class="passages">
    <header class="head">
      <div class="head-left">
        <span class="head-label">Next departures</span>
        <span v-if="stop" class="head-stop">{{ stop.libelle }}</span>
      </div>
      <div class="head-right">
        <span v-if="lastUpdate" class="age mono">{{ ageLabel }}</span>
        <button class="btn ghost refresh" :disabled="!stop || loading" @click="load" aria-label="Refresh">
          <span :class="{ spinning: loading }">↻</span>
        </button>
      </div>
    </header>

    <div v-if="!stop" class="empty">
      <span class="muted">Pick a stop</span>
      <span class="ghost">Click a stop in the list or on the map.</span>
    </div>
    <div v-else-if="error" class="empty rouge-text">{{ error }}</div>
    <div v-else-if="loading && !filteredPassages.length" class="empty muted">Loading</div>
    <div v-else-if="!filteredPassages.length" class="empty muted">No upcoming passages</div>

    <ol v-else class="board scroll">
      <li v-for="(p, i) in filteredPassages" :key="i" class="dep">
        <span class="t mono tabular">{{ p.estimated || '—' }}</span>
        <div class="meta">
          <div class="line">
            <span class="line-chip mono">{{ p.lineCode || p.lineName || '·' }}</span>
            <span class="terminus">{{ p.terminusName || p.lineName || '—' }}</span>
          </div>
          <div class="bottom">
            <span class="wait" :class="{ imm: timeUntil(p).mins === 0 }">
              {{ timeUntil(p).label }}
            </span>
            <span v-if="delayDelta(p)"
              class="delta"
              :class="{ late: delayDelta(p)!.value > 0, early: delayDelta(p)!.value < 0 }"
            >{{ delayDelta(p)!.label }}</span>
            <span v-if="p.scheduled && p.scheduled !== p.estimated" class="sched mono">
              sched {{ p.scheduled }}
            </span>
          </div>
        </div>
      </li>
    </ol>
  </section>
</template>

<style scoped>
.passages {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.head {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 12px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--rule);
}
.head-left { display: flex; flex-direction: column; gap: 2px; min-width: 0; }
.head-label {
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.10em;
  text-transform: uppercase;
  color: var(--ink-soft);
}
.head-stop {
  font-size: 14px;
  font-weight: 500;
  color: var(--ink);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.head-right {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-shrink: 0;
}
.age { font-size: 11px; color: var(--ink-faint); }

.refresh { width: 30px; height: 30px; padding: 0; font-size: 14px; }
.spinning { display: inline-block; animation: spin 1.2s linear infinite; }
@keyframes spin { to { transform: rotate(360deg); } }

.empty {
  padding: 24px 12px;
  text-align: center;
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
}
.rouge-text { color: var(--rouge); }

.board {
  list-style: none;
  margin: 0;
  padding: 0;
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.dep {
  display: grid;
  grid-template-columns: auto 1fr;
  align-items: center;
  gap: 14px;
  padding: 11px 4px;
  border-bottom: 1px solid var(--rule);
}
.dep:last-child { border-bottom: 0; }

.t {
  font-size: 18px;
  font-weight: 500;
  color: var(--ink);
  min-width: 56px;
  letter-spacing: 0.01em;
}

.meta { min-width: 0; display: flex; flex-direction: column; gap: 4px; }

.line {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}
.line-chip {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 22px;
  height: 18px;
  padding: 0 6px;
  font-size: 10px;
  font-weight: 600;
  background: rgba(241, 234, 214, 0.10);
  border: 1px solid var(--rule);
  color: var(--ink);
  border-radius: 4px;
  flex-shrink: 0;
}
.terminus {
  font-size: 13px;
  color: var(--ink);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.bottom {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 11px;
  color: var(--ink-soft);
}
.wait {
  color: var(--amber);
  font-weight: 500;
  font-variant-numeric: tabular-nums;
}
.wait.imm {
  color: var(--bg-deep);
  background: var(--amber);
  padding: 1px 7px;
  border-radius: 999px;
  font-weight: 600;
}

.delta {
  font-family: var(--font-mono);
  font-size: 10px;
}
.delta.late  { color: var(--rouge); }
.delta.early { color: var(--mint); }

.sched { color: var(--ink-faint); font-size: 10px; }
</style>
