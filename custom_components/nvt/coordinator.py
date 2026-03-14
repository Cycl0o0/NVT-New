from __future__ import annotations

import asyncio
from dataclasses import dataclass
from datetime import datetime, timedelta
import logging
import math

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_SCAN_INTERVAL
from homeassistant.core import HomeAssistant
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed
from homeassistant.util import dt as dt_util

from .api import NvtAlert, NvtApiClient, NvtApiError, NvtPassage, NvtVehicle
from .const import (
    CONF_DIRECTION_NAME,
    CONF_LINE_COLOR_BG,
    CONF_LINE_COLOR_FG,
    CONF_LINE_CODE,
    CONF_LINE_ID,
    CONF_LINE_NAME,
    CONF_LINE_TYPE,
    CONF_NETWORK,
    CONF_STOP_GROUP_KEY,
    CONF_STOP_GROUP_NAME,
    CONF_THRESHOLD_MINUTES,
    DEFAULT_SCAN_INTERVAL,
    DEFAULT_THRESHOLD_MINUTES,
    DIRECTION_ALL,
    DOMAIN,
    MIN_SCAN_INTERVAL,
)


@dataclass(frozen=True, slots=True)
class NvtMonitorData:
    network: str
    stop_group_key: str
    stop_group_name: str
    direction_name: str
    line_id: int
    line_code: str
    line_name: str
    line_type: str
    line_color_bg: str
    line_color_fg: str
    threshold_minutes: int
    fetched_at: datetime
    next_passage: NvtPassage | None
    next_passage_at: datetime | None
    minutes_remaining: int | None
    matching_count: int
    within_threshold: bool
    alerts: tuple[NvtAlert, ...]
    vehicles: tuple[NvtVehicle, ...]


def resolve_passage_datetime(passage: NvtPassage, now: datetime) -> datetime | None:
    raw_value = passage.display_time
    if not raw_value or ":" not in raw_value:
        return None

    try:
        hour_str, minute_str = raw_value.split(":", 1)
        candidate = now.replace(
            hour=int(hour_str),
            minute=int(minute_str),
            second=0,
            microsecond=0,
        )
    except ValueError:
        return None

    if candidate < now - timedelta(hours=12):
        candidate += timedelta(days=1)

    return candidate


def alert_priority(alert: NvtAlert) -> int:
    severity = alert.severity.lower()
    if severity.startswith("3") or severity in {"critical", "high"}:
        return 3
    if severity.startswith("2") or severity in {"warning", "medium"}:
        return 2
    return 1


class NvtCoordinator(DataUpdateCoordinator[NvtMonitorData]):
    def __init__(
        self,
        hass: HomeAssistant,
        client: NvtApiClient,
        entry: ConfigEntry,
    ) -> None:
        self.client = client
        self.entry = entry
        self.network = str(entry.data[CONF_NETWORK])
        self.stop_group_key = str(entry.data[CONF_STOP_GROUP_KEY])
        self.stop_group_name = str(entry.data.get(CONF_STOP_GROUP_NAME, self.stop_group_key))
        self.direction_name = str(entry.options.get(CONF_DIRECTION_NAME, entry.data.get(CONF_DIRECTION_NAME, "")))
        if self.direction_name == DIRECTION_ALL:
            self.direction_name = ""
        self.line_id = int(entry.data[CONF_LINE_ID])
        self.line_code = str(entry.data.get(CONF_LINE_CODE, ""))
        self.line_name = str(entry.data.get(CONF_LINE_NAME, ""))
        self.line_type = str(entry.data.get(CONF_LINE_TYPE, ""))
        self.line_color_bg = str(entry.data.get(CONF_LINE_COLOR_BG, ""))
        self.line_color_fg = str(entry.data.get(CONF_LINE_COLOR_FG, ""))
        self.threshold_minutes = int(
            entry.options.get(CONF_THRESHOLD_MINUTES, DEFAULT_THRESHOLD_MINUTES)
        )

        update_interval = timedelta(
            seconds=max(
                int(entry.options.get(CONF_SCAN_INTERVAL, DEFAULT_SCAN_INTERVAL)),
                MIN_SCAN_INTERVAL,
            )
        )

        super().__init__(
            hass,
            logger=logging.getLogger(__name__),
            name=f"{DOMAIN}_{entry.entry_id}",
            update_interval=update_interval,
        )

    async def _async_update_data(self) -> NvtMonitorData:
        try:
            passages, alerts = await asyncio.gather(
                self.client.async_get_passages(self.stop_group_key),
                self.client.async_get_alerts(),
            )
        except NvtApiError as err:
            raise UpdateFailed(str(err)) from err
        try:
            vehicles = await self.client.async_get_vehicles(self.line_id)
        except NvtApiError:
            vehicles = []

        now = dt_util.now()
        matching: list[tuple[datetime, NvtPassage]] = []
        line_code = self.line_code
        line_name = self.line_name
        line_type = self.line_type
        line_color_bg = self.line_color_bg
        line_color_fg = self.line_color_fg

        for passage in passages:
            if not self._passage_matches_line(passage):
                continue
            if not self._passage_matches_direction(passage):
                continue

            candidate = resolve_passage_datetime(passage, now)
            if candidate is None:
                continue

            if passage.line_code:
                line_code = passage.line_code
            if passage.line_name:
                line_name = passage.line_name
            if passage.line_type:
                line_type = passage.line_type
            if passage.color_bg:
                line_color_bg = passage.color_bg
            if passage.color_fg:
                line_color_fg = passage.color_fg

            matching.append((candidate, passage))

        matching.sort(key=lambda item: item[0])

        next_passage_at: datetime | None = None
        next_passage: NvtPassage | None = None
        for candidate, passage in matching:
            if candidate >= now - timedelta(minutes=1):
                next_passage_at = candidate
                next_passage = passage
                break

        seconds_remaining: int | None = None
        if next_passage_at is not None:
            seconds_remaining = int((next_passage_at - now).total_seconds())

        minutes_remaining = (
            max(math.ceil(seconds_remaining / 60), 0)
            if seconds_remaining is not None
            else None
        )
        within_threshold = (
            seconds_remaining is not None
            and 0 <= seconds_remaining <= self.threshold_minutes * 60
        )

        matching_alerts = tuple(
            sorted(
                (alert for alert in alerts if self._alert_matches_line(alert, line_code)),
                key=alert_priority,
                reverse=True,
            )
        )
        for alert in matching_alerts:
            if alert.line_type and not line_type:
                line_type = alert.line_type
            if alert.color_bg and not line_color_bg:
                line_color_bg = alert.color_bg
            if alert.color_fg and not line_color_fg:
                line_color_fg = alert.color_fg

        matching_vehicles = tuple(
            vehicle
            for vehicle in vehicles
            if self._vehicle_matches_direction(vehicle)
        )

        return NvtMonitorData(
            network=self.network,
            stop_group_key=self.stop_group_key,
            stop_group_name=self.stop_group_name,
            direction_name=self.direction_name,
            line_id=self.line_id,
            line_code=line_code,
            line_name=line_name,
            line_type=line_type,
            line_color_bg=line_color_bg,
            line_color_fg=line_color_fg,
            threshold_minutes=self.threshold_minutes,
            fetched_at=now,
            next_passage=next_passage,
            next_passage_at=next_passage_at,
            minutes_remaining=minutes_remaining,
            matching_count=len(matching),
            within_threshold=within_threshold,
            alerts=matching_alerts,
            vehicles=matching_vehicles,
        )

    def _passage_matches_line(self, passage: NvtPassage) -> bool:
        if passage.line_id and passage.line_id == self.line_id:
            return True
        return bool(self.line_code and passage.line_code == self.line_code)

    def _passage_matches_direction(self, passage: NvtPassage) -> bool:
        if not self.direction_name:
            return True
        return passage.terminus_name.casefold() == self.direction_name.casefold()

    def _alert_matches_line(self, alert: NvtAlert, line_code: str) -> bool:
        if alert.line_id and alert.line_id == self.line_id:
            return True
        if line_code and alert.line_code == line_code:
            return True
        return bool(line_code and line_code in alert.line_codes)

    def _vehicle_matches_direction(self, vehicle: NvtVehicle) -> bool:
        if not self.direction_name:
            return True
        return vehicle.terminus.casefold() == self.direction_name.casefold()
