/**
 * Lazy MapKit JS loader.
 *
 * MapKit JS itself is pulled in by the deferred <script> in nuxt.config.ts head.
 * Components await `$mapkit.ready()` to get the global namespace once the
 * async-init handshake has completed.
 *
 * Tokens are fetched from `/api/mapkit-token` rather than baked in at build
 * time. MapKit invokes `authorizationCallback` again whenever its token nears
 * expiry, and because we re-fetch on every call, a short-lived (server-minted)
 * token rolls over transparently — fixing the "map dies after the token
 * expires" problem.
 */
declare global {
  interface Window {
    mapkit?: any
  }
}

export default defineNuxtPlugin(() => {
  const config = useRuntimeConfig()
  // Optional build-time token kept only as a last-resort fallback if the
  // token endpoint is unreachable mid-session.
  const fallbackToken = (config.public.mapkitToken as string) || ''

  let initPromise: Promise<any> | null = null

  async function fetchToken(): Promise<string> {
    try {
      const res = await $fetch<{ token?: string }>('/api/mapkit-token', { retry: 1 })
      if (res?.token) return res.token
    } catch {
      // Fall through to the build-time token, if any.
    }
    if (fallbackToken) return fallbackToken
    throw new Error('Unable to obtain a MapKit token — set MAPKIT_PRIVATE_KEY (+ KEY_ID/TEAM_ID) or MAPKIT_JS_TOKEN.')
  }

  function waitForMapkit(timeoutMs = 15000): Promise<any> {
    return new Promise((resolve, reject) => {
      if (window.mapkit) return resolve(window.mapkit)
      const start = Date.now()
      const interval = setInterval(() => {
        if (window.mapkit) {
          clearInterval(interval)
          resolve(window.mapkit)
        } else if (Date.now() - start > timeoutMs) {
          clearInterval(interval)
          reject(new Error('MapKit JS failed to load (timeout)'))
        }
      }, 100)
    })
  }

  function load(): Promise<any> {
    if (typeof window === 'undefined') {
      return Promise.reject(new Error('MapKit only loads in the browser'))
    }
    if (window.mapkit && (window.mapkit as any)._nvtReady) {
      return Promise.resolve(window.mapkit)
    }
    if (initPromise) return initPromise

    initPromise = (async () => {
      const mk = await waitForMapkit()

      // Validate up front so configuration errors surface in the UI before we
      // try to build a map. Throws → callers show the "Map unavailable" overlay.
      await fetchToken()

      mk.init({
        // Re-fetched on every call → expiring tokens refresh themselves.
        authorizationCallback: (done: (token: string) => void) => {
          fetchToken()
            .then(done)
            .catch((err) => {
              // First init is guarded by the await above; later refresh
              // failures are non-fatal (MapKit keeps the previous token).
              console.error('[mapkit] token refresh failed:', err?.message || err)
            })
        },
        language: navigator.language || 'en'
      })
      ;(mk as any)._nvtReady = true
      return mk
    })()

    // Reset on failure so a later retry can re-attempt init.
    initPromise.catch(() => { initPromise = null })

    return initPromise
  }

  return {
    provide: {
      mapkit: { ready: load }
    }
  }
})
