from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from aiohttp import ClientError, ClientSession, ClientTimeout


@dataclass(frozen=True, slots=True)
class NvtLine:
    gid: int
    code: str
    name: str
    line_type: str
    color_bg: str
    color_fg: str


@dataclass(frozen=True, slots=True)
class NvtStopGroup:
    key: str
    name: str


@dataclass(frozen=True, slots=True)
class NvtPassage:
    line_id: int
    line_code: str
    line_name: str
    line_type: str
    color_bg: str
    color_fg: str
    terminus_name: str
    estimated: str
    scheduled: str
    live: bool
    delayed: bool

    @property
    def display_time(self) -> str:
        return self.estimated or self.scheduled


@dataclass(frozen=True, slots=True)
class NvtAlert:
    alert_id: str
    title: str
    message: str
    severity: str
    scope: str
    line_id: int
    line_code: str
    line_name: str
    line_type: str
    color_bg: str
    color_fg: str
    line_codes: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class NvtVehicle:
    gid: int
    lon: float
    lat: float
    direction: str
    terminus: str
    current_stop_name: str
    next_stop_name: str
    speed: int
    delayed: bool
    live: bool
    vehicle_type: str


class NvtApiError(Exception):
    """Base client error."""


class NvtApiConnectionError(NvtApiError):
    """Transport-level client error."""


class NvtApiResponseError(NvtApiError):
    """Unexpected HTTP or payload error."""


def normalize_base_url(base_url: str) -> str:
    cleaned = base_url.strip().rstrip("/")
    if cleaned and "://" not in cleaned:
        cleaned = f"http://{cleaned}"
    return cleaned


class NvtApiClient:
    def __init__(self, base_url: str, network: str, session: ClientSession) -> None:
        self._base_url = normalize_base_url(base_url)
        self._network = network
        self._session = session

    @property
    def base_url(self) -> str:
        return self._base_url

    @property
    def network(self) -> str:
        return self._network

    async def async_get_lines(self) -> list[NvtLine]:
        payload = await self._async_get_json("/api/lines")
        items = payload.get("items")
        if not isinstance(items, list):
            raise NvtApiResponseError("Missing lines payload.")

        lines = [
            NvtLine(
                gid=int(item["gid"]),
                code=str(item.get("code", "")),
                name=str(item.get("libelle", "")),
                line_type=str(item.get("lineType", item.get("vehicule", ""))),
                color_bg=str(item.get("colorBg", "")),
                color_fg=str(item.get("colorFg", "")),
            )
            for item in items
            if isinstance(item, dict) and "gid" in item
        ]
        return sorted(lines, key=lambda line: (line.code, line.name, line.gid))

    async def async_get_stop_groups(self) -> list[NvtStopGroup]:
        payload = await self._async_get_json("/api/stop-groups")
        items = payload.get("items")
        if not isinstance(items, list):
            raise NvtApiResponseError("Missing stop groups payload.")

        groups = [
            NvtStopGroup(
                key=str(item["key"]),
                name=str(item.get("libelle", "")),
            )
            for item in items
            if isinstance(item, dict) and "key" in item
        ]
        return sorted(groups, key=lambda group: (group.name, group.key))

    async def async_get_passages(self, stop_group_key: str) -> list[NvtPassage]:
        payload = await self._async_get_json(f"/api/stop-groups/{stop_group_key}/passages")
        items = payload.get("items")
        if not isinstance(items, list):
            raise NvtApiResponseError("Missing passages payload.")

        passages = [
            NvtPassage(
                line_id=int(item.get("lineId", 0)),
                line_code=str(item.get("lineCode", "")),
                line_name=str(item.get("lineName", "")),
                line_type=str(item.get("lineType", item.get("vehicule", ""))),
                color_bg=str(item.get("colorBg", "")),
                color_fg=str(item.get("colorFg", "")),
                terminus_name=str(item.get("terminusName", "")),
                estimated=str(item.get("estimated", "")),
                scheduled=str(item.get("scheduled", "")),
                live=bool(item.get("live", False)),
                delayed=bool(item.get("delayed", False)),
            )
            for item in items
            if isinstance(item, dict)
        ]
        return [passage for passage in passages if passage.display_time]

    async def async_get_alerts(self) -> list[NvtAlert]:
        payload = await self._async_get_json("/api/alerts")
        items = payload.get("items")
        if not isinstance(items, list):
            raise NvtApiResponseError("Missing alerts payload.")

        alerts: list[NvtAlert] = []
        for item in items:
            if not isinstance(item, dict):
                continue

            raw_codes = item.get("lineCodes", [])
            if isinstance(raw_codes, list):
                line_codes = tuple(str(code) for code in raw_codes if code)
            else:
                line_codes = ()

            if not line_codes and item.get("lineCode"):
                line_codes = (str(item.get("lineCode", "")),)

            alerts.append(
                NvtAlert(
                    alert_id=str(item.get("id", item.get("gid", ""))),
                    title=str(item.get("titre", "")),
                    message=str(item.get("message", "")),
                    severity=str(item.get("severite", "")),
                    scope=str(item.get("scope", "")),
                    line_id=int(item.get("ligneId", 0)),
                    line_code=str(item.get("lineCode", "")),
                    line_name=str(item.get("lineName", "")),
                    line_type=str(item.get("lineType", item.get("vehicule", ""))),
                    color_bg=str(item.get("colorBg", "")),
                    color_fg=str(item.get("colorFg", "")),
                    line_codes=line_codes,
                )
            )

        return alerts

    async def async_get_vehicles(self, line_id: int) -> list[NvtVehicle]:
        payload = await self._async_get_json(f"/api/lines/{line_id}/vehicles")
        items = payload.get("items")
        if not isinstance(items, list):
            raise NvtApiResponseError("Missing vehicles payload.")

        vehicles = [
            NvtVehicle(
                gid=int(item.get("gid", 0)),
                lon=float(item.get("lon", 0)),
                lat=float(item.get("lat", 0)),
                direction=str(item.get("sens", "")),
                terminus=str(item.get("terminus", "")),
                current_stop_name=str(item.get("currentStopName", "")),
                next_stop_name=str(item.get("nextStopName", "")),
                speed=int(item.get("vitesse", 0)),
                delayed=bool(
                    item.get("delayed", False)
                    or str(item.get("etat", "")).upper() == "RETARD"
                ),
                live=bool(item.get("live", False) or str(item.get("statut", "")).upper() == "REALTIME"),
                vehicle_type=str(item.get("vehicule", "")),
            )
            for item in items
            if isinstance(item, dict)
        ]
        return vehicles

    async def _async_get_json(self, path: str) -> dict[str, Any]:
        separator = "&" if "?" in path else "?"
        url = f"{self._base_url}{path}{separator}network={self._network}"

        try:
            async with self._session.get(
                url,
                timeout=ClientTimeout(total=10),
            ) as response:
                if response.status != 200:
                    raise NvtApiResponseError(f"Unexpected status {response.status} from {url}.")

                payload = await response.json()
        except (ClientError, TimeoutError) as err:
            raise NvtApiConnectionError(f"Unable to reach {url}.") from err
        except ValueError as err:
            raise NvtApiResponseError(f"Invalid JSON payload from {url}.") from err

        if not isinstance(payload, dict):
            raise NvtApiResponseError(f"Unexpected payload type from {url}.")

        return payload
