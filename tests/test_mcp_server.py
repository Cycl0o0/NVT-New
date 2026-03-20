from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "mcp" / "nvt_mcp_server.py"
SPEC = importlib.util.spec_from_file_location("nvt_mcp_server", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC is not None and SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeBackendClient:
    def health(self) -> dict:
        return {
            "status": "ok",
            "backend": "nvt-backend",
            "version": "1.0",
            "supportedNetworks": ["bordeaux", "toulouse", "idfm", "sncf"],
        }

    def lines(self, network: str) -> dict:
        return {
            "network": network,
            "generatedAt": "2026-03-19T10:00:00Z",
            "stats": {"total": 3, "active": 2},
            "items": [
                {
                    "gid": 10,
                    "code": "A",
                    "libelle": "Tram A",
                    "vehicule": "TRAM",
                    "active": True,
                    "sae": True,
                    "colorBg": "#FF0000",
                    "colorFg": "#FFFFFF",
                },
                {
                    "gid": 11,
                    "code": "15",
                    "libelle": "Bus 15",
                    "vehicule": "BUS",
                    "active": False,
                    "sae": True,
                    "colorBg": "#00FF00",
                    "colorFg": "#000000",
                },
                {
                    "gid": 12,
                    "code": "C",
                    "libelle": "Tram C",
                    "vehicule": "TRAM",
                    "active": True,
                    "sae": True,
                    "colorBg": "#0000FF",
                    "colorFg": "#FFFFFF",
                },
            ],
        }

    def alerts(self, network: str) -> dict:
        return {
            "network": network,
            "generatedAt": "2026-03-19T10:00:00Z",
            "stats": {"total": 2, "critical": 1, "warning": 1, "info": 0},
            "items": [
                {
                    "id": "alert-A",
                    "titre": "Incident A",
                    "message": "Delay on Tram A",
                    "severite": "3_critical",
                    "ligneId": 10,
                    "lineCode": "A",
                    "lineName": "Tram A",
                    "lineType": "TRAM",
                    "lineCodes": ["A"],
                },
                {
                    "id": "alert-C",
                    "titre": "Info C",
                    "message": "Maintenance",
                    "severite": "2_warning",
                    "ligneId": 12,
                    "lineCode": "C",
                    "lineName": "Tram C",
                    "lineType": "TRAM",
                    "lineCodes": ["C"],
                },
            ],
        }

    def stop_groups(self, network: str, line_gid: int | None = None) -> dict:
        if network == "idfm" and line_gid is None:
            raise MODULE.BackendError("Backend returned HTTP 400 for /api/stop-groups. Use line=<gid>.")
        items = [
            {
                "key": "group-100",
                "libelle": "Gare Saint-Jean",
                "groupe": "Bordeaux",
                "platformCount": 2,
                "gids": [100, 101],
                "lines": "A C",
            },
            {
                "key": "group-200",
                "libelle": "Victoire",
                "groupe": "Bordeaux",
                "platformCount": 1,
                "gids": [200],
                "lines": "15",
            },
        ]
        return {"network": network, "items": items, "total": len(items)}

    def passages(self, network: str, stop_key: str, line_gid: int | None = None) -> dict:
        if stop_key != "group-100":
            return {"network": network, "items": [], "stats": {"total": 0}}
        items = [
            {
                "lineId": 10,
                "lineCode": "A",
                "lineName": "Tram A",
                "lineType": "TRAM",
                "estimated": "10:05",
                "scheduled": "10:04",
                "terminusName": "Le Haillan",
                "live": True,
                "delayed": True,
            },
            {
                "lineId": 10,
                "lineCode": "A",
                "lineName": "Tram A",
                "lineType": "TRAM",
                "estimated": "10:12",
                "scheduled": "10:12",
                "terminusName": "Le Haillan",
                "live": True,
                "delayed": False,
            },
            {
                "lineId": 12,
                "lineCode": "C",
                "lineName": "Tram C",
                "lineType": "TRAM",
                "estimated": "10:08",
                "scheduled": "10:08",
                "terminusName": "Parc Expo",
                "live": True,
                "delayed": False,
            },
        ]
        return {
            "network": network,
            "items": items,
            "stats": {"total": len(items)},
            "group": {"key": "group-100", "libelle": "Gare Saint-Jean"},
        }

    def vehicles(self, network: str, line_gid: int) -> dict:
        if line_gid != 10:
            return {"network": network, "stats": {"total": 0}, "items": []}
        return {
            "network": network,
            "line": {"gid": 10, "code": "A", "libelle": "Tram A"},
            "stats": {"total": 2, "delayed": 1, "stopped": 1},
            "items": [
                {
                    "gid": 900,
                    "sens": "ALLER",
                    "terminus": "Le Haillan",
                    "etat": "RETARD",
                    "statut": "REALTIME",
                    "vitesse": 18,
                    "arret": False,
                    "currentStopName": "Porte de Bourgogne",
                    "nextStopName": "Sainte-Catherine",
                    "hasPosition": True,
                    "lon": -0.57,
                    "lat": 44.84,
                    "delayLabel": "+1m00",
                },
                {
                    "gid": 901,
                    "sens": "RETOUR",
                    "terminus": "Floirac",
                    "etat": "NORMAL",
                    "statut": "REALTIME",
                    "vitesse": 0,
                    "arret": True,
                    "currentStopName": "Victoire",
                    "nextStopName": "Saint-Nicolas",
                    "hasPosition": True,
                    "lon": -0.58,
                    "lat": 44.83,
                    "delayLabel": "",
                },
            ],
        }


class BrokenVehicleBackendClient(FakeBackendClient):
    def vehicles(self, network: str, line_gid: int) -> dict:
        return {"network": network, "bounds": {"minLon": 0, "minLat": 0, "maxLon": 1, "maxLat": 1}}


class McpServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.server = MODULE.NvtMcpServer(MODULE.NvtToolService(FakeBackendClient()))

    def call(self, method: str, params: dict | None = None, msg_id: int = 1) -> dict:
        responses = self.server.handle_message(
            {
                "jsonrpc": "2.0",
                "id": msg_id,
                "method": method,
                "params": params or {},
            }
        )
        self.assertEqual(len(responses), 1)
        return responses[0]["result"]

    def initialize(self) -> None:
        result = self.call(
            "initialize",
            {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "test", "version": "1.0.0"},
            },
        )
        self.assertEqual(result["protocolVersion"], "2025-06-18")
        self.server.handle_message({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def test_initialize_and_list_tools(self) -> None:
        self.initialize()
        result = self.call("tools/list")
        tool_names = {tool["name"] for tool in result["tools"]}
        self.assertIn("nvt_monitor_line", tool_names)
        self.assertIn("nvt_next_passages", tool_names)

    def test_line_status_returns_alerts_and_vehicles(self) -> None:
        self.initialize()
        result = self.call(
            "tools/call",
            {"name": "nvt_line_status", "arguments": {"network": "bordeaux", "line": "A"}},
        )
        structured = result["structuredContent"]
        self.assertFalse(result["isError"])
        self.assertEqual(structured["line"]["code"], "A")
        self.assertTrue(structured["status"]["active"])
        self.assertEqual(structured["alerts"]["count"], 1)
        self.assertEqual(structured["vehicles"]["count"], 2)

    def test_next_passages_filters_line_and_direction(self) -> None:
        self.initialize()
        result = self.call(
            "tools/call",
            {
                "name": "nvt_next_passages",
                "arguments": {
                    "network": "bordeaux",
                    "line": "A",
                    "stop": "Gare Saint",
                    "direction": "Le Haillan",
                    "limit": 2,
                },
            },
        )
        structured = result["structuredContent"]
        self.assertFalse(result["isError"])
        self.assertEqual(structured["stop"]["key"], "group-100")
        self.assertEqual(structured["line"]["code"], "A")
        self.assertEqual(structured["totalMatches"], 2)
        self.assertEqual(len(structured["items"]), 2)
        self.assertEqual(structured["items"][0]["lineCode"], "A")
        self.assertEqual(structured["items"][0]["terminusName"], "Le Haillan")

    def test_search_stops_requires_line_for_idfm(self) -> None:
        self.initialize()
        with self.assertRaises(MODULE.JsonRpcError):
            self.server.handle_message(
                {
                    "jsonrpc": "2.0",
                    "id": 7,
                    "method": "tools/call",
                    "params": {
                        "name": "nvt_search_stops",
                        "arguments": {"network": "idfm", "query": "Gare"},
                    },
                }
            )

    def test_line_status_flags_invalid_vehicle_payload(self) -> None:
        broken_server = MODULE.NvtMcpServer(MODULE.NvtToolService(BrokenVehicleBackendClient()))
        result = broken_server.handle_message(
            {
                "jsonrpc": "2.0",
                "id": 9,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-06-18",
                    "capabilities": {},
                    "clientInfo": {"name": "test", "version": "1.0.0"},
                },
            }
        )
        self.assertEqual(result[0]["result"]["protocolVersion"], "2025-06-18")
        broken_server.handle_message({"jsonrpc": "2.0", "method": "notifications/initialized"})
        call_result = broken_server.handle_message(
            {
                "jsonrpc": "2.0",
                "id": 10,
                "method": "tools/call",
                "params": {
                    "name": "nvt_line_status",
                    "arguments": {"network": "bordeaux", "line": "A"},
                },
            }
        )
        payload = call_result[0]["result"]
        self.assertTrue(payload["isError"])
        self.assertIn("vehicle payload without an items list", payload["content"][0]["text"])


if __name__ == "__main__":
    unittest.main()
