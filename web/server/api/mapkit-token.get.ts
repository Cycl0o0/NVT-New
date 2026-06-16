/**
 * Mints Apple MapKit JS authorization tokens for the browser.
 *
 * MapKit JS calls its `authorizationCallback` on init *and* again whenever the
 * current token nears expiry. The client plugin hits this endpoint each time,
 * so a short-lived token rolls over transparently instead of the map silently
 * dying once a static token expires.
 *
 * Two modes, in priority order:
 *   1. Signing material configured (MAPKIT_PRIVATE_KEY + MAPKIT_KEY_ID +
 *      MAPKIT_TEAM_ID) → mint a fresh ES256 JWT here, server-side. This is the
 *      self-refreshing path.
 *   2. Only a pre-issued MAPKIT_JS_TOKEN is set → hand that back as-is
 *      (legacy/static fallback; will stop working when the JWT expires).
 */
import { createPrivateKey, sign as signData } from 'node:crypto'

// `node:crypto` / `Buffer` are typed via server/mapkit-shims.d.ts (this repo
// omits @types/node); the symbols are provided by the Node/Nitro runtime.

interface MintedToken {
  token: string
  /** Unix epoch seconds at which the token expires. */
  exp: number
}

// Cache the minted token across requests so we don't re-sign on every callback.
let cached: MintedToken | null = null

function base64url(input: string | Uint8Array): string {
  return Buffer.from(input)
    .toString('base64')
    .replace(/=+$/g, '')
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
}

function mint(privateKeyPem: string, keyId: string, teamId: string, ttlSec: number): MintedToken {
  const now = Math.floor(Date.now() / 1000)
  const exp = now + ttlSec

  const header = { alg: 'ES256', kid: keyId, typ: 'JWT' }
  const payload = { iss: teamId, iat: now, exp }

  const signingInput = `${base64url(JSON.stringify(header))}.${base64url(JSON.stringify(payload))}`
  const key = createPrivateKey(privateKeyPem)
  // ES256 wants the raw r||s signature (JOSE / IEEE-P1363), not ASN.1 DER.
  const signature = signData('sha256', Buffer.from(signingInput), { key, dsaEncoding: 'ieee-p1363' })

  return { token: `${signingInput}.${base64url(signature)}`, exp }
}

export default defineEventHandler((event) => {
  const config = useRuntimeConfig(event)

  // Env vars often carry the PEM with literal "\n" — normalise back to real newlines.
  const privateKey = ((config.mapkitPrivateKey as string) || '').replace(/\\n/g, '\n').trim()
  const keyId = (config.mapkitKeyId as string) || ''
  const teamId = (config.mapkitTeamId as string) || ''
  const ttl = Number(config.mapkitTokenTtl) || 1800
  const staticToken = (config.public.mapkitToken as string) || ''

  setHeader(event, 'cache-control', 'no-store')

  if (privateKey && keyId && teamId) {
    const now = Math.floor(Date.now() / 1000)
    // Re-mint when there's no cache or we're within 60s of expiry.
    if (!cached || cached.exp - now < 60) {
      try {
        cached = mint(privateKey, keyId, teamId, ttl)
      } catch (err: any) {
        throw createError({
          statusCode: 500,
          statusMessage: 'Failed to sign MapKit token',
          data: { message: err?.message ?? 'unknown signing error' }
        })
      }
    }
    return { token: cached.token, expiresAt: cached.exp }
  }

  if (staticToken) {
    return { token: staticToken }
  }

  throw createError({
    statusCode: 500,
    statusMessage: 'MapKit token not configured',
    data: {
      hint: 'Set MAPKIT_PRIVATE_KEY + MAPKIT_KEY_ID + MAPKIT_TEAM_ID for auto-refreshing tokens, or a static MAPKIT_JS_TOKEN.'
    }
  })
})
