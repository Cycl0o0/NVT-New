/**
 * Server-side proxy to the local NVT C backend.
 *
 * Browser hits /api/nvt/<endpoint>?network=...&line=... and Nuxt forwards it
 * to NVT_BACKEND_URL. Keeps the backend address private and sidesteps CORS.
 */
export default defineEventHandler(async (event) => {
  const { backendUrl } = useRuntimeConfig(event)
  const path = (getRouterParam(event, 'path') || '').replace(/^\/+/, '')
  const query = getQuery(event)

  const url = new URL(`/api/${path}`, backendUrl)
  for (const [k, v] of Object.entries(query)) {
    if (v === undefined || v === null) continue
    if (Array.isArray(v)) v.forEach((item) => url.searchParams.append(k, String(item)))
    else url.searchParams.set(k, String(v))
  }

  try {
    return await ($fetch as any)(url.toString(), {
      method: 'GET',
      headers: { accept: 'application/json' },
      retry: 0,
      timeout: 10_000
    })
  } catch (err: any) {
    const status = err?.response?.status ?? err?.statusCode ?? 502
    throw createError({
      statusCode: status,
      statusMessage: status === 502 ? 'NVT backend unreachable' : 'Backend error',
      data: {
        upstream: url.toString(),
        message: err?.message ?? 'Unknown error',
        body: err?.data
      }
    })
  }
})
