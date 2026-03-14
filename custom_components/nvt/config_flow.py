from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.const import CONF_NAME, CONF_SCAN_INTERVAL, CONF_URL
from homeassistant.core import callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import NvtApiClient, NvtApiConnectionError, NvtApiError, normalize_base_url
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
    DEFAULT_API_URL,
    DEFAULT_NETWORK,
    DEFAULT_SCAN_INTERVAL,
    DEFAULT_THRESHOLD_MINUTES,
    DIRECTION_ALL,
    DOMAIN,
    MAX_SCAN_INTERVAL,
    MIN_SCAN_INTERVAL,
    NETWORK_LABELS,
)


class NvtConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._name = ""
        self._api_url = DEFAULT_API_URL
        self._network = DEFAULT_NETWORK
        self._all_lines: list[dict[str, Any]] = []
        self._lines_by_gid: dict[int, dict[str, Any]] = {}
        self._lines_by_code: dict[str, dict[str, Any]] = {}
        self._served_lines: list[dict[str, Any]] = []
        self._stop_groups: list[dict[str, Any]] = []
        self._directions: list[str] = []
        self._pending_entry: dict[str, Any] = {}

    async def async_step_user(self, user_input: dict[str, Any] | None = None):
        errors: dict[str, str] = {}

        if user_input is not None:
            self._name = str(user_input.get(CONF_NAME, "")).strip()
            self._api_url = normalize_base_url(str(user_input[CONF_URL]))
            self._network = str(user_input[CONF_NETWORK])

            try:
                await self._async_load_choices()
            except NvtApiConnectionError:
                errors["base"] = "cannot_connect"
            except NvtApiError:
                errors["base"] = "invalid_response"
            else:
                if not self._all_lines:
                    errors["base"] = "no_lines"
                elif not self._stop_groups:
                    errors["base"] = "no_stop_groups"
                else:
                    return await self.async_step_stop()

        data_schema = vol.Schema(
            {
                vol.Required(CONF_URL, default=self._api_url): str,
                vol.Required(CONF_NETWORK, default=self._network): vol.In(NETWORK_LABELS),
                vol.Optional(CONF_NAME, default=self._name): str,
            }
        )
        return self.async_show_form(step_id="user", data_schema=data_schema, errors=errors)

    async def async_step_stop(self, user_input: dict[str, Any] | None = None):
        errors: dict[str, str] = {}

        if user_input is not None:
            stop_group_key = str(user_input[CONF_STOP_GROUP_KEY])
            threshold_minutes = int(user_input[CONF_THRESHOLD_MINUTES])
            scan_interval = int(user_input[CONF_SCAN_INTERVAL])

            stop_group = next(
                (item for item in self._stop_groups if item["key"] == stop_group_key),
                None,
            )

            if stop_group is None:
                errors["base"] = "no_stop_groups"
            else:
                self._pending_entry = {
                    CONF_STOP_GROUP_KEY: stop_group_key,
                    CONF_STOP_GROUP_NAME: stop_group["name"],
                    CONF_THRESHOLD_MINUTES: threshold_minutes,
                    CONF_SCAN_INTERVAL: scan_interval,
                }
                try:
                    self._served_lines = await self._async_load_lines_for_stop(stop_group_key)
                except NvtApiConnectionError:
                    errors["base"] = "cannot_connect"
                except NvtApiError:
                    errors["base"] = "invalid_response"
                else:
                    if not self._served_lines:
                        errors["base"] = "no_lines_at_stop"
                    else:
                        return await self.async_step_line()

        stop_group_options = {item["key"]: item["name"] for item in self._stop_groups}
        data_schema = vol.Schema(
            {
                vol.Required(
                    CONF_STOP_GROUP_KEY,
                    default=next(iter(stop_group_options)),
                ): vol.In(stop_group_options),
                vol.Required(
                    CONF_THRESHOLD_MINUTES,
                    default=DEFAULT_THRESHOLD_MINUTES,
                ): vol.All(vol.Coerce(int), vol.Range(min=1)),
                vol.Required(
                    CONF_SCAN_INTERVAL,
                    default=DEFAULT_SCAN_INTERVAL,
                ): vol.All(
                    vol.Coerce(int),
                    vol.Range(min=MIN_SCAN_INTERVAL, max=MAX_SCAN_INTERVAL),
                ),
            }
        )
        return self.async_show_form(step_id="stop", data_schema=data_schema, errors=errors)

    async def async_step_line(self, user_input: dict[str, Any] | None = None):
        errors: dict[str, str] = {}

        if not self._pending_entry:
            return await self.async_step_stop()

        if user_input is not None:
            line_id = int(user_input[CONF_LINE_ID])
            line = next((item for item in self._served_lines if item["gid"] == line_id), None)

            if line is None:
                errors["base"] = "no_lines_at_stop"
            else:
                self._pending_entry.update(
                    {
                        CONF_LINE_ID: line_id,
                        CONF_LINE_CODE: line["code"],
                        CONF_LINE_NAME: line["name"],
                        CONF_LINE_TYPE: line["line_type"],
                        CONF_LINE_COLOR_BG: line["color_bg"],
                        CONF_LINE_COLOR_FG: line["color_fg"],
                    }
                )
                try:
                    self._directions = await self._async_load_directions(
                        str(self._pending_entry[CONF_STOP_GROUP_KEY]),
                        line_id,
                        line["code"],
                    )
                except NvtApiConnectionError:
                    errors["base"] = "cannot_connect"
                except NvtApiError:
                    errors["base"] = "invalid_response"
                else:
                    if self._directions:
                        return await self.async_step_direction()
                    return await self._async_create_monitor("")

        if not self._served_lines:
            return await self.async_step_stop()

        line_options = {item["gid"]: item["label"] for item in self._served_lines}
        data_schema = vol.Schema(
            {
                vol.Required(
                    CONF_LINE_ID,
                    default=next(iter(line_options)),
                ): vol.In(line_options),
            }
        )
        return self.async_show_form(step_id="line", data_schema=data_schema, errors=errors)

    async def async_step_direction(self, user_input: dict[str, Any] | None = None):
        if not self._pending_entry:
            return await self.async_step_stop()

        direction_options = {DIRECTION_ALL: "Toutes directions"}
        for direction in self._directions:
            direction_options[direction] = direction

        if user_input is not None:
            direction_name = str(user_input[CONF_DIRECTION_NAME])
            if direction_name == DIRECTION_ALL:
                direction_name = ""
            return await self._async_create_monitor(direction_name)

        data_schema = vol.Schema(
            {
                vol.Required(
                    CONF_DIRECTION_NAME,
                    default=DIRECTION_ALL,
                ): vol.In(direction_options),
            }
        )
        return self.async_show_form(step_id="direction", data_schema=data_schema)

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: config_entries.ConfigEntry):
        return NvtOptionsFlow(config_entry)

    async def _async_load_choices(self) -> None:
        client = NvtApiClient(self._api_url, self._network, async_get_clientsession(self.hass))
        lines = await client.async_get_lines()
        stop_groups = await client.async_get_stop_groups()

        self._all_lines = []
        self._lines_by_gid = {}
        self._lines_by_code = {}
        for line in lines:
            entry = {
                "gid": line.gid,
                "code": line.code,
                "name": line.name,
                "line_type": line.line_type,
                "color_bg": line.color_bg,
                "color_fg": line.color_fg,
            }
            entry["label"] = self._format_line_label(entry)
            self._all_lines.append(entry)
            self._lines_by_gid[line.gid] = entry
            if line.code and line.code not in self._lines_by_code:
                self._lines_by_code[line.code] = entry

        self._stop_groups = [
            {
                "key": group.key,
                "name": group.name,
            }
            for group in stop_groups
        ]

    async def _async_load_lines_for_stop(self, stop_group_key: str) -> list[dict[str, Any]]:
        client = NvtApiClient(self._api_url, self._network, async_get_clientsession(self.hass))
        passages = await client.async_get_passages(stop_group_key)

        served_lines: dict[int, dict[str, Any]] = {}
        for passage in passages:
            line_entry = None
            if passage.line_id:
                line_entry = self._lines_by_gid.get(passage.line_id)
            if line_entry is None and passage.line_code:
                line_entry = self._lines_by_code.get(passage.line_code)
            if line_entry is None:
                continue

            merged = dict(line_entry)
            if passage.line_code:
                merged["code"] = passage.line_code
            if passage.line_name:
                merged["name"] = passage.line_name
            if passage.line_type:
                merged["line_type"] = passage.line_type
            if passage.color_bg:
                merged["color_bg"] = passage.color_bg
            if passage.color_fg:
                merged["color_fg"] = passage.color_fg
            merged["label"] = self._format_line_label(merged)
            served_lines[merged["gid"]] = merged

        return sorted(
            served_lines.values(),
            key=lambda item: (item["code"], item["name"], item["gid"]),
        )

    async def _async_load_directions(
        self,
        stop_group_key: str,
        line_id: int,
        line_code: str,
    ) -> list[str]:
        client = NvtApiClient(self._api_url, self._network, async_get_clientsession(self.hass))
        passages = await client.async_get_passages(stop_group_key)

        directions = sorted(
            {
                passage.terminus_name
                for passage in passages
                if passage.terminus_name
                and (
                    (passage.line_id and passage.line_id == line_id)
                    or (line_code and passage.line_code == line_code)
                )
            }
        )
        return directions

    @staticmethod
    def _format_line_label(line: dict[str, Any]) -> str:
        label = " | ".join(part for part in (line["code"], line["name"]) if part)
        details = []
        if line.get("line_type"):
            details.append(str(line["line_type"]))
        colors = " / ".join(
            color for color in (line.get("color_bg", ""), line.get("color_fg", "")) if color
        )
        if colors:
            details.append(colors)
        if details:
            return f"{label} [{' | '.join(details)}]"
        return label

    async def _async_create_monitor(self, direction_name: str):
        stop_group_key = str(self._pending_entry[CONF_STOP_GROUP_KEY])
        line_id = int(self._pending_entry[CONF_LINE_ID])
        unique_id = f"{self._api_url}::{self._network}::{stop_group_key}::{line_id}::{direction_name}"
        await self.async_set_unique_id(unique_id)
        self._abort_if_unique_id_configured()

        title_suffix = f" [{direction_name}]" if direction_name else ""
        title = self._name or (
            f"NVT {NETWORK_LABELS[self._network]} "
            f"{self._pending_entry[CONF_STOP_GROUP_NAME]} {self._pending_entry[CONF_LINE_CODE]}"
            f"{title_suffix}"
        )
        return self.async_create_entry(
            title=title,
            data={
                CONF_URL: self._api_url,
                CONF_NETWORK: self._network,
                CONF_STOP_GROUP_KEY: stop_group_key,
                CONF_STOP_GROUP_NAME: self._pending_entry[CONF_STOP_GROUP_NAME],
                CONF_DIRECTION_NAME: direction_name,
                CONF_LINE_ID: line_id,
                CONF_LINE_CODE: self._pending_entry[CONF_LINE_CODE],
                CONF_LINE_NAME: self._pending_entry[CONF_LINE_NAME],
                CONF_LINE_TYPE: self._pending_entry.get(CONF_LINE_TYPE, ""),
                CONF_LINE_COLOR_BG: self._pending_entry.get(CONF_LINE_COLOR_BG, ""),
                CONF_LINE_COLOR_FG: self._pending_entry.get(CONF_LINE_COLOR_FG, ""),
            },
            options={
                CONF_THRESHOLD_MINUTES: int(self._pending_entry[CONF_THRESHOLD_MINUTES]),
                CONF_SCAN_INTERVAL: int(self._pending_entry[CONF_SCAN_INTERVAL]),
            },
        )


class NvtOptionsFlow(config_entries.OptionsFlow):
    def __init__(self, config_entry: config_entries.ConfigEntry) -> None:
        self.config_entry = config_entry
        self._directions: list[str] = []

    async def async_step_init(self, user_input: dict[str, Any] | None = None):
        if user_input is not None:
            if str(user_input[CONF_DIRECTION_NAME]) == DIRECTION_ALL:
                user_input[CONF_DIRECTION_NAME] = ""
            return self.async_create_entry(title="", data=user_input)

        if not self._directions:
            self._directions = await self._async_load_directions()

        current_direction = str(self.config_entry.options.get(
            CONF_DIRECTION_NAME,
            self.config_entry.data.get(CONF_DIRECTION_NAME, ""),
        ))
        direction_options = {DIRECTION_ALL: "Toutes directions"}
        for direction in self._directions:
            direction_options[direction] = direction
        if current_direction and current_direction not in direction_options:
            direction_options[current_direction] = current_direction

        data_schema = vol.Schema(
            {
                vol.Required(
                    CONF_DIRECTION_NAME,
                    default=current_direction or DIRECTION_ALL,
                ): vol.In(direction_options),
                vol.Required(
                    CONF_THRESHOLD_MINUTES,
                    default=self.config_entry.options.get(
                        CONF_THRESHOLD_MINUTES, DEFAULT_THRESHOLD_MINUTES
                    ),
                ): vol.All(vol.Coerce(int), vol.Range(min=1)),
                vol.Required(
                    CONF_SCAN_INTERVAL,
                    default=self.config_entry.options.get(
                        CONF_SCAN_INTERVAL, DEFAULT_SCAN_INTERVAL
                    ),
                ): vol.All(
                    vol.Coerce(int),
                    vol.Range(min=MIN_SCAN_INTERVAL, max=MAX_SCAN_INTERVAL),
                ),
            }
        )
        return self.async_show_form(step_id="init", data_schema=data_schema)

    async def _async_load_directions(self) -> list[str]:
        client = NvtApiClient(
            str(self.config_entry.data[CONF_URL]),
            str(self.config_entry.data[CONF_NETWORK]),
            async_get_clientsession(self.hass),
        )
        passages = await client.async_get_passages(
            str(self.config_entry.data[CONF_STOP_GROUP_KEY])
        )
        line_id = int(self.config_entry.data[CONF_LINE_ID])
        line_code = str(self.config_entry.data.get(CONF_LINE_CODE, ""))

        return sorted(
            {
                passage.terminus_name
                for passage in passages
                if passage.terminus_name
                and (
                    (passage.line_id and passage.line_id == line_id)
                    or (line_code and passage.line_code == line_code)
                )
            }
        )
