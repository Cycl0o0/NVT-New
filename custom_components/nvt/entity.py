from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import (
    CONF_LINE_COLOR_BG,
    CONF_LINE_COLOR_FG,
    CONF_LINE_TYPE,
    DOMAIN,
)
from .coordinator import NvtCoordinator


class NvtEntity(CoordinatorEntity[NvtCoordinator]):
    _attr_has_entity_name = True

    def __init__(
        self,
        coordinator: NvtCoordinator,
        entry: ConfigEntry,
        unique_suffix: str,
        name: str,
    ) -> None:
        super().__init__(coordinator)
        self._attr_name = name
        self._attr_unique_id = f"{entry.entry_id}_{unique_suffix}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)},
            name=entry.title,
            manufacturer="NVT",
            model="Network monitor",
            configuration_url=coordinator.client.base_url,
        )

    def base_attributes(self) -> dict[str, object]:
        data = self.coordinator.data
        selected_line = (
            f"{data.line_code} | {data.line_name}"
            if data.line_code and data.line_name
            else data.line_code or data.line_name
        )
        if data.line_type:
            selected_line = f"{selected_line} [{data.line_type}]"
        attributes: dict[str, object] = {
            "api_url": self.coordinator.client.base_url,
            "network": data.network,
            "stop_group_key": data.stop_group_key,
            "stop_group_name": data.stop_group_name,
            "selected_direction": data.direction_name or "Toutes directions",
            "line_id": data.line_id,
            "line_code": data.line_code,
            "line_name": data.line_name,
            "selected_line": selected_line,
            CONF_LINE_TYPE: data.line_type,
            CONF_LINE_COLOR_BG: data.line_color_bg,
            CONF_LINE_COLOR_FG: data.line_color_fg,
            "threshold_minutes": data.threshold_minutes,
            "matching_passages": data.matching_count,
            "alert_count": len(data.alerts),
            "vehicle_count": len(data.vehicles),
            "mapped_vehicle_count": sum(
                1 for vehicle in data.vehicles if vehicle.lat or vehicle.lon
            ),
        }

        if data.next_passage is not None:
            attributes["estimated_time"] = data.next_passage.estimated
            attributes["scheduled_time"] = data.next_passage.scheduled
            attributes["terminus_name"] = data.next_passage.terminus_name
            attributes["live"] = data.next_passage.live
            attributes["delayed"] = data.next_passage.delayed
        if data.minutes_remaining is not None:
            attributes["minutes_remaining"] = data.minutes_remaining

        return attributes
