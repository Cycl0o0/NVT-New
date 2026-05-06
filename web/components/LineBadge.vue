<script setup lang="ts">
import type { NvtLine } from '~/types/nvt'

const props = defineProps<{ line: NvtLine; size?: 'sm' | 'md' | 'lg' }>()

function normalizeHex(s?: string) {
  if (!s) return undefined
  const t = s.trim()
  return t.startsWith('#') ? t : `#${t}`
}

const bg = computed(() => normalizeHex(props.line.colorBg))
const fg = computed(() => normalizeHex(props.line.colorFg))
const label = computed(() =>
  (props.line.code || props.line.libelle || String(props.line.gid)).slice(0, 4)
)
</script>

<template>
  <span
    class="line-badge"
    :class="size ? `s-${size}` : ''"
    :style="{
      background: bg,
      color: fg,
      borderColor: bg ? 'rgba(0,0,0,0.22)' : undefined
    }"
    :title="line.libelle"
  >{{ label }}</span>
</template>

<style scoped>
.line-badge.s-sm { min-width: 22px; height: 18px; font-size: 10px; padding: 0 5px; border-radius: 4px; }
.line-badge.s-md { min-width: 28px; height: 22px; font-size: 12px; padding: 0 7px; border-radius: 6px; }
.line-badge.s-lg { min-width: 36px; height: 28px; font-size: 14px; padding: 0 9px; border-radius: 7px; }
</style>
