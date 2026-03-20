#!/usr/bin/env python3

from __future__ import annotations

import argparse
import atexit
from dataclasses import dataclass
from datetime import datetime, timedelta
import json
import math
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from typing import Any
from urllib import error as urllib_error
from urllib import parse as urllib_parse
from urllib import request as urllib_request


SUPPORTED_PROTOCOL_VERSIONS = (
    "2025-11-25",
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
)
SERVER_NAME = "nvt-mcp"
SERVER_TITLE = "NVT Transit MCP"
SERVER_VERSION = "0.1.0"
DEFAULT_HTTP_TIMEOUT = 10.0
DEFAULT_START_TIMEOUT = 10.0
READONLY_ANNOTATIONS = {
    "destructiveHint": False,
    "idempotentHint": True,
    "openWorldHint": True,
    "readOnlyHint": True,
}
NETWORK_ALIASES = {
    "bordeaux": "bordeaux",
    "bdx": "bordeaux",
    "tbm": "bordeaux",
    "toulouse": "toulouse",
    "tls": "toulouse",
    "tisseo": "toulouse",
    "idfm": "idfm",
    "idf": "idfm",
    "paris": "idfm",
    "paris-idfm": "idfm",
    "iledefrance": "idfm",
    "ile-de-france": "idfm",
    "sncf": "sncf",
}
NETWORKS_REQUIRING_LINE_FOR_STOPS = {"idfm", "sncf"}


class JsonRpcError(Exception):
    def __init__(self, code: int, message: str, data: Any | None = None) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.data = data


class ToolExecutionError(Exception):
    pass


class BackendError(Exception):
    pass


@dataclass(slots=True)
class ToolSpec:
    name: str
    title: str
    description: str
    input_schema: dict[str, Any]
    handler_name: str

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "title": self.title,
            "description": self.description,
            "inputSchema": self.input_schema,
            "annotations": READONLY_ANNOTATIONS,
        }


def canonical_network(value: Any) -> str:
    key = str(value or "").strip().casefold()
    network = NETWORK_ALIASES.get(key)
    if not network:
        raise JsonRpcError(
            -32602,
            f"Unsupported network '{value}'. Use one of: bordeaux, toulouse, idfm, sncf.",
        )
    return network


def normalize_base_url(value: str) -> str:
    cleaned = value.strip().rstrip("/")
    if not cleaned:
        raise BackendError("Empty backend URL.")
    if "://" not in cleaned:
        cleaned = f"http://{cleaned}"
    return cleaned


def normalize_text(value: Any) -> str:
    raw = str(value or "").casefold()
    return "".join(ch for ch in raw if ch.isalnum())


def slugify(value: Any) -> str:
    raw = str(value or "").strip()
    out: list[str] = []
    previous_dash = False
    for ch in raw:
        if ch.isalnum():
            out.append(ch.lower())
            previous_dash = False
        elif not previous_dash:
            out.append("-")
            previous_dash = True
    while out and out[-1] == "-":
        out.pop()
    return "".join(out)


def clamp_limit(value: Any, default: int = 10, maximum: int = 50) -> int:
    if value in (None, ""):
        return default
    try:
        limit = int(value)
    except (TypeError, ValueError) as exc:
        raise JsonRpcError(-32602, f"Invalid limit '{value}'.") from exc
    if limit < 1 or limit > maximum:
        raise JsonRpcError(-32602, f"limit must be between 1 and {maximum}.")
    return limit


def as_bool(value: Any, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    text = str(value).strip().casefold()
    if text in {"1", "true", "yes", "on"}:
        return True
    if text in {"0", "false", "no", "off"}:
        return False
    raise JsonRpcError(-32602, f"Invalid boolean value '{value}'.")


def require_string_arg(params: dict[str, Any], key: str) -> str:
    value = params.get(key)
    if value is None:
        raise JsonRpcError(-32602, f"Missing required argument '{key}'.")
    text = str(value).strip()
    if not text:
        raise JsonRpcError(-32602, f"Argument '{key}' cannot be empty.")
    return text


def ensure_object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise JsonRpcError(-32602, f"{label} must be an object.")
    return value


def severity_bucket(value: Any) -> str:
    text = str(value or "").strip().casefold()
    if text.startswith("3") or text in {"critical", "high"}:
        return "critical"
    if text.startswith("2") or text in {"warning", "medium"}:
        return "warning"
    return "info"


def choose_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def load_simple_env(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}

    loaded: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        loaded[key] = value
    return loaded


def json_compact(data: Any) -> str:
    return json.dumps(data, ensure_ascii=False, separators=(",", ":"))


def json_pretty(data: Any) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True)


def coerce_line_id(value: Any) -> int | None:
    if value in (None, ""):
        return None
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if text.isdigit():
        return int(text)
    return None


def first_non_empty(*values: Any) -> str:
    for value in values:
        text = str(value or "").strip()
        if text:
            return text
    return ""


def alert_matches_line(alert: dict[str, Any], line: dict[str, Any]) -> bool:
    line_gid = int(line.get("gid", 0) or 0)
    line_code = str(line.get("code", "")).strip()
    if int(alert.get("ligneId", 0) or 0) == line_gid and line_gid:
        return True
    if line_code and str(alert.get("lineCode", "")).strip() == line_code:
        return True
    codes = alert.get("lineCodes")
    if isinstance(codes, list) and line_code:
        return line_code in {str(code) for code in codes if code}
    return False


def match_direction(candidate: str, wanted: str) -> bool:
    cand_norm = normalize_text(candidate)
    wanted_norm = normalize_text(wanted)
    if not wanted_norm:
        return True
    return cand_norm == wanted_norm or wanted_norm in cand_norm


def parse_passage_datetime(item: dict[str, Any], now: datetime) -> datetime | None:
    raw_datetime = str(item.get("datetime", "")).strip()
    if raw_datetime:
        normalized = raw_datetime.replace("Z", "+00:00")
        try:
            parsed = datetime.fromisoformat(normalized)
        except ValueError:
            parsed = None
        if parsed is not None:
            if parsed.tzinfo is not None:
                return parsed.astimezone().replace(tzinfo=None)
            return parsed

    raw_value = first_non_empty(item.get("estimated"), item.get("scheduled"))
    if not raw_value or ":" not in raw_value:
        return None

    hour_text, minute_text = raw_value.split(":", 1)
    try:
        candidate = now.replace(
            hour=int(hour_text),
            minute=int(minute_text),
            second=0,
            microsecond=0,
        )
    except ValueError:
        return None

    if candidate < now - timedelta(hours=12):
        candidate += timedelta(days=1)
    return candidate


def enrich_passage(item: dict[str, Any], now: datetime) -> dict[str, Any]:
    enriched = dict(item)
    candidate = parse_passage_datetime(item, now)
    display_time = first_non_empty(item.get("estimated"), item.get("scheduled"))
    if not display_time and candidate is not None:
        display_time = candidate.strftime("%H:%M")
    enriched["displayTime"] = display_time
    if candidate is not None:
        delta_seconds = max(int((candidate - now).total_seconds()), 0)
        enriched["minutesUntil"] = max(math.ceil(delta_seconds / 60), 0)
        enriched["dueAt"] = candidate.isoformat(timespec="minutes")
    else:
        enriched["minutesUntil"] = None
        enriched["dueAt"] = ""
    return enriched


def line_summary(line: dict[str, Any]) -> dict[str, Any]:
    return {
        "gid": int(line.get("gid", 0) or 0),
        "code": str(line.get("code", "")),
        "name": str(line.get("libelle", "")),
        "type": str(first_non_empty(line.get("lineType"), line.get("vehicule"))),
        "active": bool(line.get("active", False)),
        "sae": bool(line.get("sae", False)),
        "ref": str(line.get("ref", "")),
        "colorBg": str(line.get("colorBg", "")),
        "colorFg": str(line.get("colorFg", "")),
    }


def stop_summary(stop: dict[str, Any]) -> dict[str, Any]:
    return {
        "key": str(stop.get("key", "")),
        "name": str(stop.get("libelle", "")),
        "group": str(stop.get("groupe", "")),
        "platformCount": int(stop.get("platformCount", 0) or 0),
        "lines": str(stop.get("lines", "")),
        "mode": str(stop.get("mode", "")),
        "lon": stop.get("lon"),
        "lat": stop.get("lat"),
        "gids": stop.get("gids", []),
    }


def alert_summary(alert: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(first_non_empty(alert.get("id"), alert.get("gid"))),
        "title": str(alert.get("titre", "")),
        "message": str(alert.get("message", "")),
        "severity": severity_bucket(alert.get("severite")),
        "rawSeverity": str(alert.get("severite", "")),
        "scope": str(alert.get("scope", "")),
        "lineId": int(alert.get("ligneId", 0) or 0),
        "lineCode": str(alert.get("lineCode", "")),
        "lineName": str(alert.get("lineName", "")),
        "lineType": str(first_non_empty(alert.get("lineType"), alert.get("vehicule"))),
        "lineCodes": list(alert.get("lineCodes", [])) if isinstance(alert.get("lineCodes"), list) else [],
        "colorBg": str(alert.get("colorBg", "")),
        "colorFg": str(alert.get("colorFg", "")),
    }


def vehicle_summary(vehicle: dict[str, Any]) -> dict[str, Any]:
    return {
        "gid": int(vehicle.get("gid", 0) or 0),
        "direction": str(vehicle.get("sens", "")),
        "terminus": str(vehicle.get("terminus", "")),
        "status": str(first_non_empty(vehicle.get("statut"), vehicle.get("etat"))),
        "delayed": bool(vehicle.get("delayed", False) or str(vehicle.get("etat", "")).upper() == "RETARD"),
        "live": bool(vehicle.get("live", False) or str(vehicle.get("statut", "")).upper() == "REALTIME"),
        "speed": int(vehicle.get("vitesse", 0) or 0),
        "stopped": bool(vehicle.get("arret", False)),
        "currentStop": str(vehicle.get("currentStopName", "")),
        "nextStop": str(vehicle.get("nextStopName", "")),
        "hasPosition": bool(vehicle.get("hasPosition", False)),
        "lon": vehicle.get("lon"),
        "lat": vehicle.get("lat"),
        "delayLabel": str(vehicle.get("delayLabel", "")),
        "tone": str(vehicle.get("tone", "")),
    }


class NvtBackendClient:
    def __init__(self, base_url: str, timeout: float = DEFAULT_HTTP_TIMEOUT) -> None:
        self.base_url = normalize_base_url(base_url)
        self.timeout = timeout

    def get_json(self, path: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        query_items = [(key, value) for key, value in (params or {}).items() if value not in (None, "")]
        query = urllib_parse.urlencode(query_items, doseq=True)
        url = f"{self.base_url}{path}"
        if query:
            separator = "&" if "?" in url else "?"
            url = f"{url}{separator}{query}"

        request = urllib_request.Request(
            url,
            headers={
                "Accept": "application/json",
                "User-Agent": f"{SERVER_NAME}/{SERVER_VERSION}",
            },
        )

        try:
            with urllib_request.urlopen(request, timeout=self.timeout) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except urllib_error.HTTPError as exc:
            detail = ""
            try:
                error_payload = json.loads(exc.read().decode("utf-8"))
                if isinstance(error_payload, dict) and error_payload.get("message"):
                    detail = f" {error_payload['message']}"
            except Exception:
                detail = ""
            raise BackendError(f"Backend returned HTTP {exc.code} for {url}.{detail}") from exc
        except urllib_error.URLError as exc:
            raise BackendError(f"Unable to reach backend at {url}.") from exc
        except json.JSONDecodeError as exc:
            raise BackendError(f"Backend returned invalid JSON for {url}.") from exc

        if not isinstance(payload, dict):
            raise BackendError(f"Unexpected backend payload from {url}.")
        return payload

    def health(self) -> dict[str, Any]:
        return self.get_json("/api/health")

    def lines(self, network: str) -> dict[str, Any]:
        return self.get_json("/api/lines", {"network": network})

    def alerts(self, network: str) -> dict[str, Any]:
        return self.get_json("/api/alerts", {"network": network})

    def stop_groups(self, network: str, line_gid: int | None = None) -> dict[str, Any]:
        params: dict[str, Any] = {"network": network}
        if line_gid is not None:
            params["line"] = line_gid
        return self.get_json("/api/stop-groups", params)

    def passages(self, network: str, stop_key: str, line_gid: int | None = None) -> dict[str, Any]:
        params: dict[str, Any] = {"network": network}
        if line_gid is not None:
            params["line"] = line_gid
        return self.get_json(f"/api/stop-groups/{urllib_parse.quote(stop_key, safe='')}/passages", params)

    def vehicles(self, network: str, line_gid: int) -> dict[str, Any]:
        return self.get_json(f"/api/lines/{line_gid}/vehicles", {"network": network})


class BackendSupervisor:
    def __init__(
        self,
        repo_root: Path,
        backend_url: str | None,
        backend_bin: str | None,
        backend_port: int | None,
        start_timeout: float,
        http_timeout: float,
    ) -> None:
        self.repo_root = repo_root
        self.backend_url = backend_url
        self.backend_bin = Path(backend_bin) if backend_bin else repo_root / "nvt-backend"
        self.backend_port = backend_port
        self.start_timeout = start_timeout
        self.http_timeout = http_timeout
        self.process: subprocess.Popen[str] | None = None
        self.log_handle: Any | None = None
        self.log_path: Path | None = None
        self.client: NvtBackendClient | None = None

    def start(self) -> NvtBackendClient:
        if self.backend_url:
            self.client = NvtBackendClient(self.backend_url, timeout=self.http_timeout)
            self._wait_for_health(self.client, self.start_timeout)
            return self.client

        if not self.backend_bin.exists():
            raise BackendError(
                f"Backend binary not found at {self.backend_bin}. Build it first with `make backend`."
            )

        port = self.backend_port if self.backend_port is not None else choose_free_port()
        base_url = f"http://127.0.0.1:{port}"
        env = os.environ.copy()
        env.update(load_simple_env(self.repo_root / ".nvt-backend.env"))

        self.log_handle = tempfile.NamedTemporaryFile(
            prefix="nvt-mcp-backend-",
            suffix=".log",
            delete=False,
            mode="w+",
            encoding="utf-8",
        )
        self.log_path = Path(self.log_handle.name)

        self.process = subprocess.Popen(
            [str(self.backend_bin), str(port)],
            cwd=self.repo_root,
            stdin=subprocess.DEVNULL,
            stdout=self.log_handle,
            stderr=self.log_handle,
            text=True,
            env=env,
        )
        self.backend_url = base_url
        self.client = NvtBackendClient(base_url, timeout=self.http_timeout)

        try:
            self._wait_for_health(self.client, self.start_timeout)
        except Exception:
            self.stop()
            raise
        return self.client

    def _wait_for_health(self, client: NvtBackendClient, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None

        while time.monotonic() < deadline:
            if self.process is not None:
                return_code = self.process.poll()
                if return_code is not None:
                    details = self._read_log_tail()
                    raise BackendError(
                        f"Spawned backend exited early with code {return_code}.{details}"
                    )
            try:
                client.health()
                return
            except Exception as exc:
                last_error = exc
                time.sleep(0.2)

        raise BackendError(
            f"Timed out waiting for backend health at {client.base_url}: {last_error}"
        )

    def _read_log_tail(self) -> str:
        if not self.log_path or not self.log_path.exists():
            return ""
        try:
            lines = self.log_path.read_text(encoding="utf-8").splitlines()
        except OSError:
            return ""
        if not lines:
            return ""
        tail = "\n".join(lines[-10:])
        return f"\nBackend log tail:\n{tail}"

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.process = None

        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None

        if self.log_path is not None:
            try:
                self.log_path.unlink()
            except OSError:
                pass
            self.log_path = None


class NvtToolService:
    def __init__(self, backend: NvtBackendClient) -> None:
        self.backend = backend

    @staticmethod
    def _line_candidates(lines: list[dict[str, Any]], query: str) -> list[dict[str, Any]]:
        query_text = query.strip()
        query_norm = normalize_text(query_text)
        numeric = query_text.isdigit()

        exact: list[dict[str, Any]] = []
        fuzzy: list[dict[str, Any]] = []
        for line in lines:
            gid = int(line.get("gid", 0) or 0)
            code = str(line.get("code", ""))
            libelle = str(line.get("libelle", ""))
            ref = str(line.get("ref", ""))

            fields = [code, libelle, ref, str(gid)]
            normalized_fields = [normalize_text(field) for field in fields if field]

            if numeric and gid == int(query_text):
                exact.append(line)
                continue
            if query_norm and any(field == query_norm for field in normalized_fields):
                exact.append(line)
                continue
            if query_norm and any(query_norm in field for field in normalized_fields):
                fuzzy.append(line)

        return exact if exact else fuzzy

    @staticmethod
    def _stop_candidates(stops: list[dict[str, Any]], query: str) -> list[dict[str, Any]]:
        query_text = query.strip()
        query_norm = normalize_text(query_text)
        numeric = query_text.isdigit()

        exact: list[dict[str, Any]] = []
        fuzzy: list[dict[str, Any]] = []
        for stop in stops:
            key = str(stop.get("key", ""))
            libelle = str(stop.get("libelle", ""))
            groupe = str(stop.get("groupe", ""))
            lines = str(stop.get("lines", ""))
            gids = stop.get("gids", [])

            fields = [key, libelle, groupe, lines]
            normalized_fields = [normalize_text(field) for field in fields if field]
            gid_match = numeric and isinstance(gids, list) and int(query_text) in {
                int(gid) for gid in gids if str(gid).isdigit()
            }

            if gid_match or (query_norm and any(field == query_norm for field in normalized_fields)):
                exact.append(stop)
                continue
            if query_norm and any(query_norm in field for field in normalized_fields):
                fuzzy.append(stop)

        return exact if exact else fuzzy

    def _resolve_line(self, network: str, line_query: Any) -> dict[str, Any]:
        payload = self.backend.lines(network)
        items = payload.get("items")
        if not isinstance(items, list):
            raise ToolExecutionError("Backend returned no line list.")

        query = str(line_query).strip()
        if not query:
            raise JsonRpcError(-32602, "Argument 'line' cannot be empty.")

        matches = self._line_candidates(items, query)
        if not matches:
            raise ToolExecutionError(f"No line found for '{query}' on network '{network}'.")
        if len(matches) > 1:
            suggestions = [line_summary(line) for line in matches[:10]]
            raise ToolExecutionError(
                f"Ambiguous line '{query}' on network '{network}'. Suggestions: {json_compact(suggestions)}"
            )
        return ensure_object(matches[0], "line")

    def _resolve_stop(self, network: str, stop_query: Any, line_gid: int | None = None) -> tuple[dict[str, Any], dict[str, Any]]:
        payload = self.backend.stop_groups(network, line_gid=line_gid)
        items = payload.get("items")
        if not isinstance(items, list):
            raise ToolExecutionError("Backend returned no stop-group list.")

        query = str(stop_query).strip()
        if not query:
            raise JsonRpcError(-32602, "Argument 'stop' cannot be empty.")

        matches = self._stop_candidates(items, query)
        if not matches:
            raise ToolExecutionError(f"No stop found for '{query}' on network '{network}'.")
        if len(matches) > 1:
            suggestions = [stop_summary(stop) for stop in matches[:10]]
            raise ToolExecutionError(
                f"Ambiguous stop '{query}' on network '{network}'. Suggestions: {json_compact(suggestions)}"
            )
        return ensure_object(matches[0], "stop"), payload

    def _tool_network_overview(self, params: dict[str, Any]) -> dict[str, Any]:
        network = canonical_network(params.get("network"))
        lines_payload = self.backend.lines(network)
        alerts_payload = self.backend.alerts(network)

        lines = lines_payload.get("items", [])
        alerts = alerts_payload.get("items", [])
        if not isinstance(lines, list) or not isinstance(alerts, list):
            raise ToolExecutionError("Backend returned an invalid network overview payload.")

        summary = {
            "network": network,
            "generatedAt": first_non_empty(lines_payload.get("generatedAt"), alerts_payload.get("generatedAt")),
            "lineStats": lines_payload.get("stats", {}),
            "alertStats": alerts_payload.get("stats", {}),
            "lineCount": len(lines),
            "alertCount": len(alerts),
            "sampleLines": [line_summary(item) for item in lines[:10] if isinstance(item, dict)],
            "topAlerts": [alert_summary(item) for item in alerts[:5] if isinstance(item, dict)],
        }
        return summary

    def _tool_search_lines(self, params: dict[str, Any]) -> dict[str, Any]:
        network = canonical_network(params.get("network"))
        query = str(params.get("query", "")).strip()
        active_only = as_bool(params.get("active_only"), default=False)
        limit = clamp_limit(params.get("limit"), default=10)

        payload = self.backend.lines(network)
        items = payload.get("items", [])
        if not isinstance(items, list):
            raise ToolExecutionError("Backend returned an invalid line list.")

        filtered: list[dict[str, Any]] = []
        query_norm = normalize_text(query) if query else ""

        for raw in items:
            if not isinstance(raw, dict):
                continue
            if active_only and not bool(raw.get("active", False)):
                continue
            if query_norm:
                fields = [
                    str(raw.get("code", "")),
                    str(raw.get("libelle", "")),
                    str(raw.get("vehicule", "")),
                    str(raw.get("ref", "")),
                    str(raw.get("gid", "")),
                ]
                haystack = [normalize_text(field) for field in fields if field]
                if not any(query_norm in field for field in haystack):
                    continue
            filtered.append(line_summary(raw))

        return {
            "network": network,
            "query": query,
            "activeOnly": active_only,
            "totalMatches": len(filtered),
            "items": filtered[:limit],
        }

    def _tool_search_stops(self, params: dict[str, Any]) -> dict[str, Any]:
        network = canonical_network(params.get("network"))
        query = str(params.get("query", "")).strip()
        if not query:
            raise JsonRpcError(-32602, "Argument 'query' cannot be empty.")
        limit = clamp_limit(params.get("limit"), default=10)
        resolved_line: dict[str, Any] | None = None
        line_gid: int | None = None

        if network in NETWORKS_REQUIRING_LINE_FOR_STOPS:
            line_query = params.get("line")
            if line_query in (None, ""):
                raise JsonRpcError(-32602, f"Argument 'line' is required for network '{network}'.")
            resolved_line = self._resolve_line(network, line_query)
            line_gid = int(resolved_line.get("gid", 0) or 0)
        elif params.get("line") not in (None, ""):
            resolved_line = self._resolve_line(network, params.get("line"))
            line_gid = int(resolved_line.get("gid", 0) or 0)

        payload = self.backend.stop_groups(network, line_gid=line_gid)
        items = payload.get("items", [])
        if not isinstance(items, list):
            raise ToolExecutionError("Backend returned an invalid stop-group list.")

        matches = self._stop_candidates([item for item in items if isinstance(item, dict)], query)
        return {
            "network": network,
            "query": query,
            "line": line_summary(resolved_line) if resolved_line else None,
            "totalMatches": len(matches),
            "items": [stop_summary(item) for item in matches[:limit]],
        }

    def _tool_alerts(self, params: dict[str, Any]) -> dict[str, Any]:
        network = canonical_network(params.get("network"))
        limit = clamp_limit(params.get("limit"), default=10)
        severity_filter = str(params.get("severity", "")).strip().casefold()
        resolved_line: dict[str, Any] | None = None

        if params.get("line") not in (None, ""):
            resolved_line = self._resolve_line(network, params.get("line"))

        payload = self.backend.alerts(network)
        items = payload.get("items", [])
        if not isinstance(items, list):
            raise ToolExecutionError("Backend returned an invalid alert list.")

        filtered: list[dict[str, Any]] = []
        for raw in items:
            if not isinstance(raw, dict):
                continue
            if resolved_line is not None and not alert_matches_line(raw, resolved_line):
                continue
            alert = alert_summary(raw)
            if severity_filter and alert["severity"] != severity_filter:
                continue
            filtered.append(alert)

        filtered.sort(key=lambda item: ({"critical": 0, "warning": 1, "info": 2}.get(item["severity"], 9), item["title"]))
        return {
            "network": network,
            "line": line_summary(resolved_line) if resolved_line else None,
            "severity": severity_filter or None,
            "totalMatches": len(filtered),
            "stats": payload.get("stats", {}),
            "items": filtered[:limit],
        }

    def _tool_line_status(self, params: dict[str, Any]) -> dict[str, Any]:
        network = canonical_network(params.get("network"))
        line = self._resolve_line(network, params.get("line"))
        include_vehicles = as_bool(params.get("include_vehicles"), default=True)
        vehicle_limit = clamp_limit(params.get("vehicle_limit"), default=5)

        alerts_payload = self.backend.alerts(network)
        alerts_items = alerts_payload.get("items", [])
        if not isinstance(alerts_items, list):
            raise ToolExecutionError("Backend returned an invalid alert list.")
        matching_alerts = [alert_summary(item) for item in alerts_items if isinstance(item, dict) and alert_matches_line(item, line)]

        vehicles_stats: dict[str, Any] | None = None
        vehicles_items: list[dict[str, Any]] = []
        if include_vehicles:
            vehicles_payload = self.backend.vehicles(network, int(line.get("gid", 0) or 0))
            if "items" not in vehicles_payload:
                raise ToolExecutionError("Backend returned a vehicle payload without an items list.")
            raw_vehicles = vehicles_payload.get("items")
            if not isinstance(raw_vehicles, list):
                raise ToolExecutionError("Backend returned an invalid vehicle list.")
            vehicles_items = [vehicle_summary(item) for item in raw_vehicles if isinstance(item, dict)]
            vehicles_stats = {
                "count": len(vehicles_items),
                "stats": vehicles_payload.get("stats", {}),
                "items": vehicles_items[:vehicle_limit],
            }

        status = {
            "network": network,
            "line": line_summary(line),
            "status": {
                "active": bool(line.get("active", False)),
                "label": "active" if bool(line.get("active", False)) else "inactive",
            },
            "alerts": {
                "count": len(matching_alerts),
                "items": matching_alerts,
            },
            "vehicles": vehicles_stats,
        }
        return status

    def _tool_next_passages(self, params: dict[str, Any]) -> dict[str, Any]:
        network = canonical_network(params.get("network"))
        stop_query = require_string_arg(params, "stop")
        direction = str(params.get("direction", "")).strip()
        limit = clamp_limit(params.get("limit"), default=5)
        line: dict[str, Any] | None = None
        line_gid: int | None = None

        if network in NETWORKS_REQUIRING_LINE_FOR_STOPS or params.get("line") not in (None, ""):
            if params.get("line") in (None, ""):
                raise JsonRpcError(-32602, f"Argument 'line' is required for network '{network}'.")
            line = self._resolve_line(network, params.get("line"))
            line_gid = int(line.get("gid", 0) or 0)

        stop, _payload = self._resolve_stop(network, stop_query, line_gid=line_gid)
        passages_payload = self.backend.passages(network, str(stop.get("key", "")), line_gid=line_gid)
        items = passages_payload.get("items", [])
        if not isinstance(items, list):
            raise ToolExecutionError("Backend returned an invalid passage list.")

        now = datetime.now()
        filtered: list[dict[str, Any]] = []
        for raw in items:
            if not isinstance(raw, dict):
                continue
            if line is not None:
                same_gid = int(raw.get("lineId", 0) or 0) == int(line.get("gid", 0) or 0)
                same_code = str(raw.get("lineCode", "")) == str(line.get("code", ""))
                if not same_gid and not same_code:
                    continue
            if direction and not match_direction(str(raw.get("terminusName", "")), direction):
                continue
            filtered.append(enrich_passage(raw, now))

        filtered.sort(key=lambda item: (item.get("minutesUntil") is None, item.get("minutesUntil"), item.get("displayTime", "")))
        return {
            "network": network,
            "line": line_summary(line) if line else None,
            "stop": stop_summary(stop),
            "direction": direction or None,
            "stats": passages_payload.get("stats", {}),
            "totalMatches": len(filtered),
            "items": filtered[:limit],
        }

    def _tool_monitor_line(self, params: dict[str, Any]) -> dict[str, Any]:
        network = canonical_network(params.get("network"))
        line = self._resolve_line(network, params.get("line"))
        stop_query = str(params.get("stop", "")).strip()
        direction = str(params.get("direction", "")).strip()
        passages_limit = clamp_limit(params.get("passages_limit"), default=5)
        vehicle_limit = clamp_limit(params.get("vehicle_limit"), default=5)

        line_status = self._tool_line_status(
            {
                "network": network,
                "line": str(line.get("gid", "")),
                "include_vehicles": True,
                "vehicle_limit": vehicle_limit,
            }
        )

        next_passages = None
        if stop_query:
            next_passages = self._tool_next_passages(
                {
                    "network": network,
                    "line": str(line.get("gid", "")),
                    "stop": stop_query,
                    "direction": direction,
                    "limit": passages_limit,
                }
            )

        return {
            "network": network,
            "line": line_status["line"],
            "status": line_status["status"],
            "alerts": line_status["alerts"],
            "vehicles": line_status["vehicles"],
            "stopMonitor": next_passages,
        }

    def call_tool(self, name: str, params: dict[str, Any]) -> dict[str, Any]:
        handler = getattr(self, self._tool_handler_name(name), None)
        if handler is None:
            raise JsonRpcError(-32601, f"Unknown tool '{name}'.")
        return ensure_object(handler(params), "tool result")

    @staticmethod
    def _tool_handler_name(name: str) -> str:
        return {
            "nvt_network_overview": "_tool_network_overview",
            "nvt_search_lines": "_tool_search_lines",
            "nvt_search_stops": "_tool_search_stops",
            "nvt_alerts": "_tool_alerts",
            "nvt_line_status": "_tool_line_status",
            "nvt_next_passages": "_tool_next_passages",
            "nvt_monitor_line": "_tool_monitor_line",
        }.get(name, "")


class NvtMcpServer:
    def __init__(self, tool_service: NvtToolService) -> None:
        self.tool_service = tool_service
        self.protocol_version: str | None = None
        self.client_initialized = False
        self.tools = [
            ToolSpec(
                name="nvt_network_overview",
                title="NVT Network Overview",
                description="Summarize one NVT network with line counts and current alert counts.",
                input_schema={
                    "type": "object",
                    "properties": {
                        "network": {
                            "type": "string",
                            "enum": ["bordeaux", "toulouse", "idfm", "sncf"],
                            "description": "Transit network to inspect.",
                        }
                    },
                    "required": ["network"],
                    "additionalProperties": False,
                },
                handler_name="_tool_network_overview",
            ),
            ToolSpec(
                name="nvt_search_lines",
                title="Search NVT Lines",
                description="Search lines by code, gid, name, or ref on one NVT network.",
                input_schema={
                    "type": "object",
                    "properties": {
                        "network": {
                            "type": "string",
                            "enum": ["bordeaux", "toulouse", "idfm", "sncf"],
                        },
                        "query": {
                            "type": "string",
                            "description": "Optional partial code, gid, or line name.",
                        },
                        "active_only": {
                            "type": "boolean",
                            "description": "Only return active lines.",
                        },
                        "limit": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 50,
                        },
                    },
                    "required": ["network"],
                    "additionalProperties": False,
                },
                handler_name="_tool_search_lines",
            ),
            ToolSpec(
                name="nvt_search_stops",
                title="Search NVT Stops",
                description="Search stop groups or stop areas by name or key. For IDFM and SNCF, a line is required.",
                input_schema={
                    "type": "object",
                    "properties": {
                        "network": {
                            "type": "string",
                            "enum": ["bordeaux", "toulouse", "idfm", "sncf"],
                        },
                        "query": {
                            "type": "string",
                            "description": "Partial stop name, key, or gid.",
                        },
                        "line": {
                            "type": ["string", "integer"],
                            "description": "Line gid, code, or name when the network requires line-scoped stops.",
                        },
                        "limit": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 50,
                        },
                    },
                    "required": ["network", "query"],
                    "additionalProperties": False,
                },
                handler_name="_tool_search_stops",
            ),
            ToolSpec(
                name="nvt_alerts",
                title="NVT Alerts",
                description="List active network alerts, optionally filtered to a specific line or severity.",
                input_schema={
                    "type": "object",
                    "properties": {
                        "network": {
                            "type": "string",
                            "enum": ["bordeaux", "toulouse", "idfm", "sncf"],
                        },
                        "line": {
                            "type": ["string", "integer"],
                            "description": "Optional line gid, code, or name.",
                        },
                        "severity": {
                            "type": "string",
                            "enum": ["critical", "warning", "info"],
                        },
                        "limit": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 50,
                        },
                    },
                    "required": ["network"],
                    "additionalProperties": False,
                },
                handler_name="_tool_alerts",
            ),
            ToolSpec(
                name="nvt_line_status",
                title="NVT Line Status",
                description="Show whether a line is active and return its alert and vehicle status.",
                input_schema={
                    "type": "object",
                    "properties": {
                        "network": {
                            "type": "string",
                            "enum": ["bordeaux", "toulouse", "idfm", "sncf"],
                        },
                        "line": {
                            "type": ["string", "integer"],
                            "description": "Line gid, code, or name.",
                        },
                        "include_vehicles": {
                            "type": "boolean",
                            "description": "Fetch current vehicles for the line.",
                        },
                        "vehicle_limit": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 50,
                        },
                    },
                    "required": ["network", "line"],
                    "additionalProperties": False,
                },
                handler_name="_tool_line_status",
            ),
            ToolSpec(
                name="nvt_next_passages",
                title="NVT Next Passages",
                description="Return upcoming departures for a stop group. For IDFM and SNCF, a line is required.",
                input_schema={
                    "type": "object",
                    "properties": {
                        "network": {
                            "type": "string",
                            "enum": ["bordeaux", "toulouse", "idfm", "sncf"],
                        },
                        "stop": {
                            "type": "string",
                            "description": "Stop key, gid, or partial stop name.",
                        },
                        "line": {
                            "type": ["string", "integer"],
                            "description": "Optional line filter. Required for IDFM and SNCF.",
                        },
                        "direction": {
                            "type": "string",
                            "description": "Optional terminus name filter.",
                        },
                        "limit": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 50,
                        },
                    },
                    "required": ["network", "stop"],
                    "additionalProperties": False,
                },
                handler_name="_tool_next_passages",
            ),
            ToolSpec(
                name="nvt_monitor_line",
                title="NVT Monitor Line",
                description="Composite monitor for one line: active state, alerts, vehicles, and optional next departures at a stop.",
                input_schema={
                    "type": "object",
                    "properties": {
                        "network": {
                            "type": "string",
                            "enum": ["bordeaux", "toulouse", "idfm", "sncf"],
                        },
                        "line": {
                            "type": ["string", "integer"],
                            "description": "Line gid, code, or name.",
                        },
                        "stop": {
                            "type": "string",
                            "description": "Optional stop key, gid, or partial stop name.",
                        },
                        "direction": {
                            "type": "string",
                            "description": "Optional terminus filter for departures.",
                        },
                        "passages_limit": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 50,
                        },
                        "vehicle_limit": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 50,
                        },
                    },
                    "required": ["network", "line"],
                    "additionalProperties": False,
                },
                handler_name="_tool_monitor_line",
            ),
        ]
        self.tools_by_name = {tool.name: tool for tool in self.tools}

    def _result(self, request_id: Any, payload: dict[str, Any]) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "result": payload}

    def _error(self, request_id: Any, error: JsonRpcError) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {
                "code": error.code,
                "message": error.message,
            },
        }
        if error.data is not None:
            payload["error"]["data"] = error.data
        return payload

    def _tool_success(self, data: dict[str, Any]) -> dict[str, Any]:
        return {
            "content": [{"type": "text", "text": json_pretty(data)}],
            "structuredContent": data,
            "isError": False,
        }

    def _tool_failure(self, message: str) -> dict[str, Any]:
        data = {"error": message}
        return {
            "content": [{"type": "text", "text": message}],
            "structuredContent": data,
            "isError": True,
        }

    def handle_message(self, message: dict[str, Any]) -> list[dict[str, Any]]:
        if not isinstance(message, dict):
            raise JsonRpcError(-32600, "Invalid Request")

        if message.get("jsonrpc") != "2.0":
            raise JsonRpcError(-32600, "Invalid Request")

        method = message.get("method")
        request_id = message.get("id")
        params = message.get("params")

        if method is None:
            return []

        if request_id is None:
            self._handle_notification(method, params)
            return []

        result = self._handle_request(method, params)
        return [self._result(request_id, result)]

    def _handle_notification(self, method: Any, params: Any) -> None:
        if method == "notifications/initialized":
            self.client_initialized = True
            return
        if method == "notifications/cancelled":
            return

    def _handle_request(self, method: Any, params: Any) -> dict[str, Any]:
        if method == "initialize":
            return self._handle_initialize(params)

        if self.protocol_version is None:
            raise JsonRpcError(-32002, "Server not initialized.")

        if method == "ping":
            return {}
        if method == "tools/list":
            return {"tools": [tool.to_json() for tool in self.tools]}
        if method == "tools/call":
            return self._handle_tools_call(params)
        if method == "logging/setLevel":
            return {}

        raise JsonRpcError(-32601, f"Method not found: {method}")

    def _handle_initialize(self, params: Any) -> dict[str, Any]:
        payload = ensure_object(params or {}, "initialize params")
        requested_version = str(payload.get("protocolVersion", "")).strip()

        if requested_version in SUPPORTED_PROTOCOL_VERSIONS:
            self.protocol_version = requested_version
        else:
            self.protocol_version = SUPPORTED_PROTOCOL_VERSIONS[0]

        return {
            "protocolVersion": self.protocol_version,
            "capabilities": {
                "tools": {
                    "listChanged": False,
                }
            },
            "serverInfo": {
                "name": SERVER_NAME,
                "title": SERVER_TITLE,
                "version": SERVER_VERSION,
            },
            "instructions": (
                "Use nvt_search_lines and nvt_search_stops to resolve ambiguous names, "
                "then call nvt_line_status, nvt_next_passages, or nvt_monitor_line."
            ),
        }

    def _handle_tools_call(self, params: Any) -> dict[str, Any]:
        payload = ensure_object(params or {}, "tools/call params")
        name = require_string_arg(payload, "name")
        arguments = ensure_object(payload.get("arguments") or {}, "tool arguments")

        if name not in self.tools_by_name:
            raise JsonRpcError(-32601, f"Unknown tool '{name}'.")

        try:
            result = self.tool_service.call_tool(name, arguments)
            return self._tool_success(result)
        except JsonRpcError:
            raise
        except (ToolExecutionError, BackendError) as exc:
            return self._tool_failure(str(exc))

    def run(self) -> None:
        for raw_line in sys.stdin:
            line = raw_line.strip()
            if not line:
                continue

            try:
                parsed = json.loads(line)
            except json.JSONDecodeError:
                self._write_message(
                    self._error(None, JsonRpcError(-32700, "Parse error"))
                )
                continue

            batch = parsed if isinstance(parsed, list) else [parsed]
            responses: list[dict[str, Any]] = []
            for item in batch:
                try:
                    responses.extend(self.handle_message(item))
                except JsonRpcError as exc:
                    request_id = item.get("id") if isinstance(item, dict) else None
                    responses.append(self._error(request_id, exc))
                except Exception as exc:
                    responses.append(self._error(item.get("id") if isinstance(item, dict) else None, JsonRpcError(-32603, f"Internal error: {exc}")))

            if isinstance(parsed, list):
                if responses:
                    self._write_message(responses)
            else:
                for response in responses:
                    self._write_message(response)

    @staticmethod
    def _write_message(message: dict[str, Any] | list[dict[str, Any]]) -> None:
        sys.stdout.write(json_compact(message))
        sys.stdout.write("\n")
        sys.stdout.flush()


def build_arg_parser() -> argparse.ArgumentParser:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="NVT MCP stdio server")
    parser.add_argument(
        "--backend-url",
        default=os.getenv("NVT_BACKEND_URL", ""),
        help="Use an already running NVT backend instead of spawning one.",
    )
    parser.add_argument(
        "--backend-bin",
        default=os.getenv("NVT_BACKEND_BIN", str(repo_root / "nvt-backend")),
        help="Path to the nvt-backend binary when auto-spawning.",
    )
    parser.add_argument(
        "--backend-port",
        type=int,
        default=int(os.getenv("NVT_BACKEND_PORT", "0") or "0") or None,
        help="Port to use when auto-spawning nvt-backend. Defaults to a free local port.",
    )
    parser.add_argument(
        "--backend-start-timeout",
        type=float,
        default=float(os.getenv("NVT_MCP_BACKEND_START_TIMEOUT", str(DEFAULT_START_TIMEOUT))),
        help="Seconds to wait for the backend health check.",
    )
    parser.add_argument(
        "--http-timeout",
        type=float,
        default=float(os.getenv("NVT_MCP_HTTP_TIMEOUT", str(DEFAULT_HTTP_TIMEOUT))),
        help="Seconds to wait for backend HTTP calls.",
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()
    repo_root = Path(__file__).resolve().parents[1]

    supervisor = BackendSupervisor(
        repo_root=repo_root,
        backend_url=args.backend_url or None,
        backend_bin=args.backend_bin,
        backend_port=args.backend_port,
        start_timeout=args.backend_start_timeout,
        http_timeout=args.http_timeout,
    )
    atexit.register(supervisor.stop)

    try:
        backend = supervisor.start()
    except Exception as exc:
        print(f"{SERVER_NAME}: {exc}", file=sys.stderr)
        return 1

    server = NvtMcpServer(NvtToolService(backend))
    try:
        server.run()
    finally:
        supervisor.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
