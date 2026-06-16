// Ambient shims for the Node built-ins used by server routes.
//
// This repo intentionally omits @types/node (see the `declare const process`
// shim in nuxt.config.ts). These ambient declarations make `node:crypto` and
// `Buffer` resolve for typecheck; the real implementations are provided by the
// Node/Nitro runtime. Keep this a global (no top-level import/export) so the
// `declare module` below is a declaration rather than an augmentation.

declare module 'node:crypto' {
  export function createPrivateKey(key: string): unknown
  export function sign(
    algorithm: string,
    data: unknown,
    options: { key: unknown; dsaEncoding: string }
  ): Uint8Array
}

declare const Buffer: {
  from(data: string | Uint8Array, encoding?: string): { toString(encoding: string): string }
}
