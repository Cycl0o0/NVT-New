<script setup lang="ts">
import { NETWORKS, type Network } from '~/types/nvt'

const model = defineModel<Network>({ required: true })
defineProps<{ compact?: boolean }>()
</script>

<template>
  <nav class="switcher" :class="{ compact }" role="tablist" aria-label="Network">
    <button
      v-for="net in NETWORKS"
      :key="net.slug"
      class="net-btn"
      :class="{ active: model === net.slug }"
      :style="{ '--net-accent': net.accent }"
      :aria-selected="model === net.slug"
      role="tab"
      :title="`${net.label} · ${net.city}`"
      @click="model = net.slug"
    >
      <span class="net-marker" aria-hidden="true"></span>
      <span class="net-label">{{ net.label }}</span>
      <span v-if="!compact" class="net-city">{{ net.city }}</span>
    </button>
  </nav>
</template>

<style scoped>
.switcher {
  display: inline-flex;
  align-items: stretch;
  padding: 3px;
  gap: 2px;
  background: rgba(0, 0, 0, 0.22);
  border: 1px solid var(--rule);
  border-radius: 12px;
}

.switcher.compact { padding: 3px; }

.net-btn {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1px;
  padding: 6px 14px;
  background: transparent;
  border: 0;
  border-radius: 8px;
  color: var(--ink-soft);
  cursor: pointer;
  font-family: var(--font-sans);
  text-align: center;
  min-width: 0;
  transition: color 160ms ease, background 200ms ease;
}
.switcher.compact .net-btn {
  padding: 5px 10px;
  flex-direction: row;
  gap: 6px;
}

.net-btn:hover { color: var(--ink); background: rgba(241, 234, 214, 0.04); }

.net-btn.active {
  color: var(--ink);
  background: rgba(255, 184, 77, 0.06);
}

/* Glowing top marker on the active tab — restrained, single accent */
.net-marker {
  position: absolute;
  top: -3px;
  left: 14px; right: 14px;
  height: 2px;
  background: var(--amber);
  border-radius: 0 0 2px 2px;
  opacity: 0;
  transform: scaleX(0.4);
  transform-origin: center;
  transition: opacity 220ms ease, transform 220ms ease;
  box-shadow: 0 0 10px rgba(255, 184, 77, 0.7);
}
.net-btn.active .net-marker {
  opacity: 1;
  transform: scaleX(1);
}
.switcher.compact .net-btn.active .net-marker { display: none; }

.net-label {
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0;
  white-space: nowrap;
}

.net-city {
  font-family: var(--font-mono);
  font-size: 9px;
  color: var(--ink-faint);
  white-space: nowrap;
}
.switcher.compact .net-city { display: none; }
</style>
