<script setup lang="ts">
import type { NvtAlert } from '~/types/nvt'

const props = defineProps<{
  alerts: NvtAlert[]
  selectedLineGid?: number | null
}>()

function severityClass(severite: string) {
  const s = (severite || '').toLowerCase()
  if (/3|crit|major|severe/.test(s)) return 'rouge'
  if (/2|warn|moderate|delay/.test(s)) return 'ochre'
  return 'steel'
}

const filtered = computed(() => {
  if (!props.selectedLineGid) return props.alerts
  return props.alerts.filter(
    (a) => a.ligneId === props.selectedLineGid || !a.ligneId
  )
})

const open = ref<Record<string, boolean>>({})
function toggle(id: string) { open.value[id] = !open.value[id] }
function key(a: NvtAlert) { return `${a.gid}-${a.id || a.titre}` }
</script>

<template>
  <section class="alerts">
    <div v-if="!filtered.length" class="empty">
      <span class="muted">No disruptions</span>
    </div>

    <ul v-else class="list scroll">
      <li
        v-for="a in filtered"
        :key="key(a)"
        class="alert"
        :class="severityClass(a.severite)"
      >
        <button class="alert-head" @click="toggle(key(a))">
          <span class="stripe" aria-hidden="true"></span>
          <span class="meta">
            <span class="title">{{ a.titre || 'Service alert' }}</span>
            <span v-if="a.lineCode" class="line-tag mono">{{ a.lineCode }}</span>
          </span>
          <span class="caret" :class="{ rot: open[key(a)] }" aria-hidden="true">›</span>
        </button>
        <Transition name="reveal">
          <p v-if="open[key(a)]" class="msg">{{ a.message }}</p>
        </Transition>
      </li>
    </ul>
  </section>
</template>

<style scoped>
.alerts {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.empty {
  padding: 22px 12px;
  text-align: center;
  font-size: 12px;
}

.list {
  list-style: none;
  margin: 0;
  padding: 0;
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.alert { border-bottom: 1px solid var(--rule); }
.alert:last-child { border-bottom: 0; }

.alert-head {
  position: relative;
  display: grid;
  grid-template-columns: 3px 1fr auto;
  gap: 12px;
  align-items: center;
  width: 100%;
  padding: 10px 6px 10px 0;
  background: transparent;
  border: 0;
  color: var(--ink);
  text-align: left;
  cursor: pointer;
  font-family: var(--font-sans);
  transition: background 140ms ease;
}
.alert-head:hover { background: rgba(241, 234, 214, 0.04); }

.stripe {
  width: 3px;
  height: 24px;
  border-radius: 0 2px 2px 0;
  background: var(--rule-strong);
}
.alert.rouge .stripe { background: var(--rouge); box-shadow: 0 0 8px rgba(239, 90, 90, 0.35); }
.alert.ochre .stripe { background: var(--ochre); box-shadow: 0 0 8px rgba(224, 164, 100, 0.35); }
.alert.steel .stripe { background: var(--steel); }

.meta {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}
.title {
  font-size: 13px;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  flex: 1;
  min-width: 0;
}
.line-tag {
  font-size: 10px;
  color: var(--ink-soft);
  background: rgba(241, 234, 214, 0.06);
  padding: 1px 5px;
  border-radius: 3px;
  flex-shrink: 0;
}

.caret {
  font-family: var(--font-mono);
  font-size: 18px;
  line-height: 1;
  color: var(--ink-faint);
  width: 14px;
  text-align: center;
  transition: transform 200ms ease, color 200ms ease;
}
.caret.rot { transform: rotate(90deg); color: var(--amber); }

.msg {
  margin: 0 0 12px;
  padding: 0 14px 0 18px;
  font-size: 12px;
  color: var(--ink-soft);
  line-height: 1.55;
  white-space: pre-wrap;
}

.reveal-enter-active,
.reveal-leave-active {
  transition: max-height 240ms ease, opacity 200ms ease;
  overflow: hidden;
}
.reveal-enter-from,
.reveal-leave-to { max-height: 0; opacity: 0; }
.reveal-enter-to,
.reveal-leave-from { max-height: 600px; opacity: 1; }
</style>
