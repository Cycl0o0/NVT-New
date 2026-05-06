export default defineEventHandler(async (event): Promise<{ ok: boolean; backend?: unknown; error?: string; backendUrl?: string }> => {
  const { backendUrl } = useRuntimeConfig(event)
  try {
    const data = await $fetch(`${backendUrl}/api/health`, { timeout: 3000, retry: 0 })
    return { ok: true, backend: data }
  } catch (err: any) {
    return { ok: false, error: err?.message ?? 'unreachable', backendUrl }
  }
})
