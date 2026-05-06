<script setup lang="ts">
import type { NvtLine } from '~/types/nvt'

const props = defineProps<{
  lines: NvtLine[]
  selectedGid: number | null
  loading?: boolean
}>()
const emit = defineEmits<{ select: [line: NvtLine] }>()

const search = ref('')

const KIND_LABEL: Record<string, string> = {
  TRAM: 'Tram',
  METRO: 'Metro',
  TRAIN: 'Train',
  RER: 'RER',
  BUS: 'Bus',
  NAVETTE: 'Shuttle',
  AUTRE: 'Other',
}

const grouped = computed(() => {
  const q = search.value.trim().toLowerCase()
  const filtered = q
    ? props.lines.filter((l) =>
        l.libelle?.toLowerCase().includes(q) ||
        l.code?.toLowerCase().includes(q))
    : props.lines

  const buckets: Record<string, NvtLine[]> = {}
  for (const l of filtered) {
    const key = (l.vehicule || 'AUTRE').toUpperCase()
    if (!buckets[key]) buckets[key] = []
    buckets[key].push(l)
  }
  const order = ['TRAM', 'METRO', 'TRAIN', 'RER', 'BUS', 'NAVETTE', 'AUTRE']
  const sortedKeys = Object.keys(buckets).sort((a, b) => {
    const ai = order.indexOf(a); const bi = order.indexOf(b)
    return (ai === -1 ? 99 : ai) - (bi === -1 ? 99 : bi)
  })
  return sortedKeys.map((k) => ({
    kind: k,
    label: KIND_LABEL[k] ?? k,
    items: buckets[k],
  }))
})
</script>

<template>
  <section class="line-list">
    <div class="search">
      <input
        v-model="search"
        type="search"
        placeholder="Search a line"
        autocomplete="off"
        spellcheck="false"
      />
    </div>

    <div v-if="loading && !lines.length" class="empty">
      <span class="dot pulse amber"></span>
      <span class="muted">Loading</span>
    </div>
    <div v-else-if="!lines.length" class="empty">
      <span class="muted">No lines available</span>
    </div>

    <div v-else class="groups scroll">
      <section v-for="g in grouped" :key="g.kind" class="group">
        <header class="group-head">
          <span class="group-name">{{ g.label }}</span>
          <span class="group-count tabular">{{ g.items.length }}</span>
        </header>
        <ul class="rows">
          <li v-for="line in g.items" :key="line.gid">
            <button
              class="line-row"
              :class="{ active: line.gid === selectedGid }"
              @click="emit('select', line)"
            >
              <LineBadge :line="line" size="md" />
              <span class="name">{{ line.libelle }}</span>
              <span v-if="line.active === false" class="pill">off</span>
            </button>
          </li>
        </ul>
      </section>
    </div>
  </section>
</template>

<style scoped>
.line-list {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.empty {
  padding: 24px 12px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}

.groups {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 18px;
  padding-right: 2px;
}

.group-head {
  display: flex;
  align-items: baseline;
  gap: 10px;
  padding: 0 4px 6px;
  border-bottom: 1px solid var(--rule);
  margin-bottom: 4px;
}
.group-name {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.10em;
  color: var(--ink-soft);
}
.group-count {
  margin-left: auto;
  font: 500 11px var(--font-mono);
  color: var(--ink-faint);
}

.rows {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
}

.line-row {
  position: relative;
  display: flex;
  align-items: center;
  gap: 12px;
  width: 100%;
  padding: 9px 10px;
  background: transparent;
  border: 0;
  border-radius: 8px;
  color: var(--ink);
  cursor: pointer;
  font-family: var(--font-sans);
  text-align: left;
  transition: background 140ms ease;
}
.line-row:hover { background: rgba(241, 234, 214, 0.04); }

/* Subtle left bar on the active row — that's it. No fill block. */
.line-row::before {
  content: '';
  position: absolute;
  left: 0; top: 8px; bottom: 8px;
  width: 2px;
  background: transparent;
  border-radius: 1px;
  transition: background 200ms ease;
}
.line-row.active::before {
  background: var(--amber);
  box-shadow: 0 0 10px rgba(255, 184, 77, 0.6);
}
.line-row.active { background: rgba(255, 184, 77, 0.05); }
.line-row.active .name { color: var(--amber-soft); }

.name {
  flex: 1;
  font-size: 13px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
