import type {
  Network, NvtLine, NvtAlert, NvtVehicle, NvtStopGroup, NvtPassage,
  NvtRouteMap, NvtBoundary, NvtListResponse
} from '~/types/nvt'

function buildPath(endpoint: string, network: Network, extra: Record<string, string | number | undefined> = {}) {
  const params = new URLSearchParams({ network })
  for (const [k, v] of Object.entries(extra)) {
    if (v !== undefined && v !== null && v !== '') params.set(k, String(v))
  }
  return `/api/nvt/${endpoint}?${params.toString()}`
}

export function useNvtLines(network: Ref<Network>) {
  return useFetch<NvtListResponse<NvtLine>>(() => buildPath('lines', network.value), {
    key: () => `lines-${network.value}`,
    watch: [network],
    server: false
  })
}

export function useNvtAlerts(network: Ref<Network>) {
  return useFetch<NvtListResponse<NvtAlert>>(() => buildPath('alerts', network.value), {
    key: () => `alerts-${network.value}`,
    watch: [network],
    server: false
  })
}

export function useNvtStopGroups(network: Ref<Network>, lineGid: Ref<number | null>) {
  return useFetch<NvtListResponse<NvtStopGroup>>(
    () => {
      const needsLine = network.value === 'idfm' || network.value === 'sncf'
      if (needsLine && !lineGid.value) return null as any
      return buildPath('stop-groups', network.value, { line: lineGid.value ?? undefined })
    },
    {
      key: () => `stops-${network.value}-${lineGid.value ?? 'all'}`,
      watch: [network, lineGid],
      server: false
    }
  )
}

export async function fetchPassages(network: Network, key: string, lineGid: number | null) {
  return await $fetch<NvtListResponse<NvtPassage>>(
    buildPath(`stop-groups/${encodeURIComponent(key)}/passages`, network, { line: lineGid ?? undefined })
  )
}

export async function fetchVehicles(network: Network, lineGid: number) {
  return await $fetch<NvtListResponse<NvtVehicle>>(
    buildPath(`lines/${lineGid}/vehicles`, network)
  )
}

export async function fetchLineRoute(network: Network, lineGid: number) {
  return await $fetch<NvtRouteMap>(buildPath(`lines/${lineGid}/route`, network))
}

export async function fetchBoundaries(network: Network) {
  return await $fetch<NvtBoundary>(buildPath('map/boundaries', network))
}
