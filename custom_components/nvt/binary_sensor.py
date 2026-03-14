from __future__ import annotations

from homeassistant.components.binary_sensor import BinarySensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .entity import NvtEntity


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    runtime_data = hass.data[DOMAIN][entry.entry_id]
    coordinator = runtime_data["coordinator"]
    async_add_entities([NvtWithinThresholdBinarySensor(coordinator, entry)])


class NvtWithinThresholdBinarySensor(NvtEntity, BinarySensorEntity):
    _attr_icon = "mdi:bell-ring-outline"

    def __init__(self, coordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator, entry, "within_threshold", "Passage within threshold")

    @property
    def is_on(self) -> bool:
        return self.coordinator.data.within_threshold

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        return self.base_attributes()
