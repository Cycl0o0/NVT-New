<script setup lang="ts">
import type { NvtStopGroup } from '~/types/nvt'

const props = defineProps<{
  stops: NvtStopGroup[]
  selectedKey: string | null
  loading?: boolean
}>()
const emit = defineEmits<{ select: [stop: NvtStopGroup] }>()

const search = ref('')
const filtered = computed(() => {
  const q = search.value.trim().toLowerCase()
  if (!q) return props.stops
  return props.stops.filter((s) =>
    s.libelle.toLowerCase().includes(q) ||
    (s.groupe || '').toLowerCase().includes(q)
  )
})
</script>

<template>
  <section class="stop-list">
    <div class="search">
      <input
        v-model="search"
        type="search"
        placeholder="Search a stop"
        autocomplete="off"
      />
    </div>

    <div v-if="loading && !stops.length" class="empty muted">Loading</div>
    <div v-else-if="!stops.length" class="empty muted">Pick a line first</div>

    <ul v-else class="track scroll">
      <li
        v-for="stop in filtered"
        :key="stop.key"
        class="stop"
        :class="{ active: stop.key === selectedKey }"
      >
        <button class="stop-row" @click="emit('select', stop)">
          <span class="rail" aria-hidden="true">
            <span class="node"></span>
          </span>
          <span class="body">
            <span class="name">{{ stop.libelle }}</span>
            <span
              v-if="stop.groupe && stop.groupe.toLowerCase() !== stop.libelle.toLowerCase()"
              class="zone"
            >{{ stop.groupe }}</span>
          </span>
          <span v-if="stop.platformCount > 1" class="pill tabular">{{ stop.platformCount }}</span>
        </button>
      </li>
    </ul>
  </section>
</template>

<style scoped>
.stop-list {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.empty {
  padding: 20px 12px;
  text-align: center;
  font-size: 12px;
}

/* Track-line metaphor — dotted vertical "rail" with a node at each stop.
   This is functional, not decorative: it implies they're on a route. */
.track {
  list-style: none;
  margin: 0;
  padding: 4px 0;
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.stop { position: relative; }

.stop-row {
  display: grid;
  grid-template-columns: 18px 1fr auto;
  align-items: stretch;
  gap: 10px;
  width: 100%;
  padding: 8px 6px 8px 4px;
  background: transparent;
  border: 0;
  color: var(--ink);
  cursor: pointer;
  text-align: left;
  font-family: var(--font-sans);
  transition: background 140ms ease;
}
.stop-row:hover { background: rgba(241, 234, 214, 0.04); border-radius: 6px; }

.rail {
  position: relative;
  display: flex;
  justify-content: center;
}
.rail::before {
  content: '';
  position: absolute;
  top: -2px; bottom: -2px;
  left: 50%;
  width: 1px;
  margin-left: -0.5px;
  background-image: linear-gradient(
    180deg,
    var(--rule-strong) 0 4px,
    transparent 4px 8px
  );
  background-size: 1px 8px;
  background-repeat: repeat-y;
}
.stop:first-child .rail::before { top: 50%; }
.stop:last-child  .rail::before { bottom: 50%; }

.node {
  position: relative;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--bg-deep);
  border: 1.5px solid var(--rule-strong);
  margin-top: 9px;
  transition: background 200ms ease, border-color 200ms ease,
              box-shadow 200ms ease, transform 200ms ease;
}
.stop:hover .node { border-color: var(--ink-soft); }
.stop.active .node {
  background: var(--amber);
  border-color: var(--amber);
  box-shadow: 0 0 0 3px rgba(255, 184, 77, 0.16),
              0 0 12px rgba(255, 184, 77, 0.55);
}

.body {
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}
.name {
  font-size: 13px;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.stop.active .name { color: var(--amber-soft); }
.zone {
  font-family: var(--font-mono);
  font-size: 10px;
  color: var(--ink-faint);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.pill { padding: 2px 6px; font-size: 10px; align-self: flex-start; margin-top: 6px; }
</style>
