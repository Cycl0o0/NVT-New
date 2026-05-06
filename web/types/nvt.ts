export type Network = 'bdx' | 'tls' | 'idfm' | 'sncf' | 'star' | 'tcl'

export interface NvtLine {
  gid: number
  ident: number
  code: string
  libelle: string
  vehicule: string
  active?: boolean
  ref?: string
  colorBg?: string
  colorFg?: string
}

export interface NvtAlert {
  gid: number
  id: string
  titre: string
  message: string
  severite: string
  ligneId: number
  scope?: string
  lineCode?: string
  lineName?: string
  lineType?: string
}

export interface NvtVehicle {
  gid: number
  lon: number
  lat: number
  etat: string
  retard: number
  delayLabel: string
  vitesse: number
  vehicule: string
  statut: string
  sens: string
  terminus: string
  arret?: boolean
  arretActu: number
  arretSuiv: number
  currentStopName: string
  nextStopName: string
  tone: 'stable' | 'warning' | 'critical' | string
  datetime?: string
  waitingTime?: string
}

export interface NvtStopGroup {
  key: string
  libelle: string
  groupe: string
  platformCount: number
  gids: number[]
  lon?: number
  lat?: number
  ref?: string
  lines?: string
  mode?: string
  lineCodes?: string[]
}

export interface NvtPassage {
  estimated: string
  scheduled: string
  courseId: number
  lineId: number
  terminusGid: number
  terminusName: string
  lineCode: string
  lineName: string
  lineType: string
  datetime?: string
  waitingTime?: string
  stopName?: string
}

export interface NvtRoutePoint { lon: number; lat: number }
export interface NvtRoutePath {
  kind: number
  points: NvtRoutePoint[]
}
export interface NvtRouteMap {
  bounds: { minLon: number; minLat: number; maxLon: number; maxLat: number }
  paths: NvtRoutePath[]
  stats: { total: number; aller: number; retour: number }
}

export interface NvtBoundary {
  bounds: { minLon: number; minLat: number; maxLon: number; maxLat: number }
  paths: { kind: number; points: NvtRoutePoint[] }[]
  labels: { lon: number; lat: number; name: string; rank: number }[]
}

export interface NvtListResponse<T> {
  generatedAt?: string
  network?: Network
  items: T[]
  stats?: Record<string, number>
}

export interface NetworkMeta {
  slug: Network
  label: string
  city: string
  accent: string
  /** Default map view: [centerLat, centerLon, latSpan, lonSpan]. */
  view: [number, number, number, number]
  /** True when /api/map/boundaries is supported. */
  hasBoundary: boolean
}

export const NETWORKS: NetworkMeta[] = [
  { slug: 'bdx',  label: 'TBM',     city: 'Bordeaux',     accent: '#7ad7f0',
    view: [44.8378, -0.5792, 0.18, 0.30], hasBoundary: true  },
  { slug: 'tls',  label: 'Tisséo',  city: 'Toulouse',     accent: '#ff6e78',
    view: [43.6047,  1.4442, 0.18, 0.30], hasBoundary: true  },
  { slug: 'idfm', label: 'IDFM',    city: 'Paris',        accent: '#a07cff',
    view: [48.8566,  2.3522, 0.55, 0.85], hasBoundary: false },
  { slug: 'sncf', label: 'SNCF',    city: 'France',       accent: '#5db8ff',
    view: [46.6034,  1.8883, 8.50, 12.0], hasBoundary: false },
  { slug: 'star', label: 'STAR',    city: 'Rennes',       accent: '#61c3d9',
    view: [48.1147, -1.6794, 0.18, 0.30], hasBoundary: false },
  { slug: 'tcl',  label: 'TCL',     city: 'Lyon',         accent: '#e3007a',
    view: [45.7578,  4.8320, 0.20, 0.32], hasBoundary: false }
]

export function networkMeta(slug: Network): NetworkMeta {
  return NETWORKS.find((n) => n.slug === slug) ?? NETWORKS[0]
}
