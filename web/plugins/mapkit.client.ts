/**
 * Lazy MapKit JS loader.
 *
 * MapKit JS is loaded by the deferred <script> in nuxt.config.ts head.
 * Components await `$mapkit.ready()` to access the global namespace
 * after the async-init handshake has completed.
 */
declare global {
  interface Window {
    mapkit?: any
  }
}

export default defineNuxtPlugin(() => {
  const config = useRuntimeConfig()
  const token = config.public.mapkitToken as string

  let initPromise: Promise<any> | null = null

  function load(): Promise<any> {
    if (typeof window === 'undefined') {
      return Promise.reject(new Error('MapKit only loads in the browser'))
    }
    if (window.mapkit && (window.mapkit as any)._nvtReady) {
      return Promise.resolve(window.mapkit)
    }
    if (initPromise) return initPromise

    initPromise = new Promise((resolve, reject) => {
      const tryInit = () => {
        const mk = window.mapkit
        if (!mk) return false
        if (!token) {
          reject(new Error('MAPKIT_JS_TOKEN is not configured'))
          return true
        }
        mk.init({
          authorizationCallback: (done: (token: string) => void) => done(token),
          language: navigator.language || 'en'
        })
        ;(mk as any)._nvtReady = true
        resolve(mk)
        return true
      }

      if (tryInit()) return

      const start = Date.now()
      const interval = setInterval(() => {
        if (tryInit()) {
          clearInterval(interval)
        } else if (Date.now() - start > 15000) {
          clearInterval(interval)
          reject(new Error('MapKit JS failed to load (timeout)'))
        }
      }, 100)
    })

    return initPromise
  }

  return {
    provide: {
      mapkit: { ready: load }
    }
  }
})
