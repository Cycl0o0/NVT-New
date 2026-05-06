declare const process: { env: Record<string, string | undefined> }

export default defineNuxtConfig({
  compatibilityDate: '2024-11-01',
  devtools: { enabled: true },
  typescript: { strict: true },

  css: ['~/assets/css/main.css'],

  app: {
    head: {
      title: 'NVT — Live Transit',
      meta: [
        { charset: 'utf-8' },
        { name: 'viewport', content: 'width=device-width, initial-scale=1' },
        { name: 'theme-color', content: '#0a0e1a' },
        { name: 'color-scheme', content: 'dark' },
        // Tell the Dark Reader extension to skip this page — we already ship a
        // dark theme, and its DOM rewrites otherwise corrupt SSR hydration.
        { name: 'darkreader-lock' },
        { name: 'description', content: 'Live transit monitoring for Bordeaux, Toulouse, Paris IDFM and SNCF.' }
      ],
      link: [
        { rel: 'preconnect', href: 'https://cdn.apple-mapkit.com' },
        { rel: 'preconnect', href: 'https://fonts.googleapis.com' },
        { rel: 'preconnect', href: 'https://fonts.gstatic.com', crossorigin: '' },
        {
          rel: 'stylesheet',
          href: 'https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@300;400;500;600;700&family=IBM+Plex+Mono:wght@400;500&display=swap'
        }
      ],
      script: [
        {
          src: 'https://cdn.apple-mapkit.com/mk/5.x.x/mapkit.js',
          crossorigin: 'anonymous',
          'data-libraries': 'map,annotations,services,geojson',
          defer: true
        }
      ]
    }
  },

  runtimeConfig: {
    backendUrl: process.env.NVT_BACKEND_URL || 'http://127.0.0.1:8080',
    public: {
      mapkitToken: process.env.MAPKIT_JS_TOKEN || '',
      defaultNetwork: process.env.NVT_DEFAULT_NETWORK || 'bdx',
      refreshIntervalMs: Number(process.env.NVT_REFRESH_MS || 15000)
    }
  }
})
