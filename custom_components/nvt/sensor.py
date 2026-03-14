from __future__ import annotations

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.util import dt as dt_util

from .api import NvtAlert, NvtVehicle
from .const import DOMAIN
from .entity import NvtEntity


def alert_detail(alert: NvtAlert) -> str:
    return alert.message or alert.title or "Alerte sans detail"


def shorten_state(value: str) -> str:
    return value if len(value) <= 255 else f"{value[:252]}..."


def alert_attributes(alert: NvtAlert) -> dict[str, object]:
    return {
        "id": alert.alert_id,
        "detail": alert_detail(alert),
        "title": alert.title,
        "message": alert.message,
        "severity": alert.severity,
        "scope": alert.scope,
        "line_name": alert.line_name,
        "line_type": alert.line_type,
        "line_color_bg": alert.color_bg,
        "line_color_fg": alert.color_fg,
        "line_codes": list(alert.line_codes),
    }


def vehicle_attributes(vehicle: NvtVehicle) -> dict[str, object]:
    return {
        "id": vehicle.gid,
        "latitude": vehicle.lat,
        "longitude": vehicle.lon,
        "direction": vehicle.direction,
        "terminus": vehicle.terminus,
        "current_stop_name": vehicle.current_stop_name,
        "next_stop_name": vehicle.next_stop_name,
        "speed": vehicle.speed,
        "delayed": vehicle.delayed,
        "live": vehicle.live,
        "vehicle_type": vehicle.vehicle_type,
    }


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    runtime_data = hass.data[DOMAIN][entry.entry_id]
    coordinator = runtime_data["coordinator"]

    async_add_entities(
        [
            NvtSelectedLineSensor(coordinator, entry),
            NvtSelectedDirectionSensor(coordinator, entry),
            NvtNextTimestampSensor(coordinator, entry),
            NvtNextMinutesSensor(coordinator, entry),
            NvtLineAlertsSensor(coordinator, entry),
            NvtAlertDetailsSensor(coordinator, entry),
            NvtAllAlertsSensor(coordinator, entry),
            NvtLineVehiclesSensor(coordinator, entry),
        ]
    )


class NvtSelectedLineSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:bus"

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "selected_line", "Selected line")

    @property
    def native_value(self):
        data = self.coordinator.data
        if data.line_code and data.line_name:
            label = f"{data.line_code} | {data.line_name}"
        else:
            label = data.line_code or data.line_name
        if data.line_type:
            label = f"{label} [{data.line_type}]"
        return label

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        return self.base_attributes()


class NvtSelectedDirectionSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:swap-horizontal"

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "selected_direction", "Selected direction")

    @property
    def native_value(self):
        return self.coordinator.data.direction_name or "Toutes directions"

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        return self.base_attributes()


class NvtNextTimestampSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:clock-outline"
    _attr_device_class = SensorDeviceClass.TIMESTAMP

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "next_passage", "Next passage")

    @property
    def native_value(self):
        next_passage_at = self.coordinator.data.next_passage_at
        if next_passage_at is None:
            return None
        return dt_util.as_utc(next_passage_at)

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        return self.base_attributes()


class NvtNextMinutesSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:timer-outline"
    _attr_native_unit_of_measurement = UnitOfTime.MINUTES
    _attr_suggested_display_precision = 0

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "minutes_until_passage", "Minutes until passage")

    @property
    def native_value(self):
        return self.coordinator.data.minutes_remaining

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        return self.base_attributes()


class NvtLineAlertsSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:alert-outline"

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "line_alerts", "Line alerts")

    @property
    def native_value(self):
        return len(self.coordinator.data.alerts)

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        attributes = self.base_attributes()
        attributes["alerts"] = [alert_attributes(alert) for alert in self.coordinator.data.alerts]
        return attributes


class NvtAlertDetailsSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:alert-circle-outline"

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "alert_details", "Alert details")

    @property
    def native_value(self):
        if not self.coordinator.data.alerts:
            return "Aucune alerte"
        return shorten_state(alert_detail(self.coordinator.data.alerts[0]))

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        attributes = self.base_attributes()
        top_alert = self.coordinator.data.alerts[0] if self.coordinator.data.alerts else None
        if top_alert is not None:
            attributes["top_alert_id"] = top_alert.alert_id
            attributes["top_alert_title"] = top_alert.title
            attributes["top_alert_detail"] = alert_detail(top_alert)
            attributes["top_alert_message"] = top_alert.message
            attributes["top_alert_severity"] = top_alert.severity
            attributes["top_alert_scope"] = top_alert.scope
            attributes["top_alert_line_type"] = top_alert.line_type
            attributes["top_alert_line_color_bg"] = top_alert.color_bg
            attributes["top_alert_line_color_fg"] = top_alert.color_fg
            attributes["top_alert_line_codes"] = list(top_alert.line_codes)
        attributes["alerts"] = [alert_attributes(alert) for alert in self.coordinator.data.alerts]
        return attributes


class NvtAllAlertsSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:message-alert-outline"

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "all_alerts", "All alerts")

    @property
    def native_value(self):
        if not self.coordinator.data.alerts:
            return "Aucune alerte"
        messages = [alert_detail(alert) for alert in self.coordinator.data.alerts]
        return shorten_state(" | ".join(messages))

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        attributes = self.base_attributes()
        messages = [alert_detail(alert) for alert in self.coordinator.data.alerts]
        attributes["messages"] = messages
        attributes["alerts_text"] = "\n".join(messages)
        attributes["alerts"] = [alert_attributes(alert) for alert in self.coordinator.data.alerts]
        return attributes


class NvtLineVehiclesSensor(NvtEntity, SensorEntity):
    _attr_icon = "mdi:map-marker-path"

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "line_vehicles", "Line vehicles")

    @property
    def native_value(self):
        return len(self.coordinator.data.vehicles)

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        attributes = self.base_attributes()
        vehicles = [vehicle_attributes(vehicle) for vehicle in self.coordinator.data.vehicles]
        attributes["vehicles"] = vehicles
        attributes["map_positions"] = [
            {
                "id": vehicle.gid,
                "latitude": vehicle.lat,
                "longitude": vehicle.lon,
                "terminus": vehicle.terminus,
            }
            for vehicle in self.coordinator.data.vehicles
            if vehicle.lat or vehicle.lon
        ]
        return attributes
