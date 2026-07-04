#!/usr/bin/env python3
"""Small Home Assistant stress test for ChangHongIceMakerESPHome."""

from __future__ import annotations

import argparse
import json
import pathlib
import time
import urllib.parse
import urllib.request


WATCH_ENTITIES = [
    "switch.chang_hong_ice_maker_esphome_power",
    "switch.chang_hong_ice_maker_esphome_large_ice",
    "button.chang_hong_ice_maker_esphome_uv_toggle",
    "sensor.chang_hong_ice_maker_esphome_state",
    "sensor.chang_hong_ice_maker_esphome_classified_state",
    "sensor.chang_hong_ice_maker_esphome_fast_state_candidate",
    "sensor.chang_hong_ice_maker_esphome_feature_state_candidate",
    "sensor.chang_hong_ice_maker_esphome_action_state",
    "sensor.chang_hong_ice_maker_esphome_action_result",
    "binary_sensor.chang_hong_ice_maker_esphome_action_busy",
    "binary_sensor.chang_hong_ice_maker_esphome_standby_window_valid",
    "sensor.chang_hong_ice_maker_esphome_adc_signature",
    "sensor.chang_hong_ice_maker_esphome_confidence",
    "sensor.chang_hong_ice_maker_esphome_fast_ratio_0hhhh",
    "sensor.chang_hong_ice_maker_esphome_fast_ratio_mhmhh",
    "sensor.chang_hong_ice_maker_esphome_small_feature_score",
    "sensor.chang_hong_ice_maker_esphome_standby_feature_score",
    "sensor.chang_hong_ice_maker_esphome_p2_stddev",
    "sensor.chang_hong_ice_maker_esphome_delta_p2_p4",
    "sensor.chang_hong_ice_maker_esphome_delta_p5_p2",
    "sensor.chang_hong_ice_maker_esphome_standby_blink_score",
]


class HomeAssistant:
    def __init__(self, base_url: str, auth_json: pathlib.Path, client_id: str):
        self.base_url = base_url.rstrip("/")
        auth = json.loads(auth_json.read_text())
        data = urllib.parse.urlencode(
            {
                "grant_type": "refresh_token",
                "refresh_token": auth["refresh_token"],
                "client_id": client_id,
            }
        ).encode()
        with urllib.request.urlopen(self.base_url + "/auth/token", data=data, timeout=10) as response:
            self.access_token = json.loads(response.read())["access_token"]

    def request(self, path: str, method: str = "GET", body: dict | None = None):
        headers = {"Authorization": "Bearer " + self.access_token}
        data = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            data = json.dumps(body).encode()
        request = urllib.request.Request(self.base_url + path, data=data, headers=headers, method=method)
        with urllib.request.urlopen(request, timeout=10) as response:
            raw = response.read()
            return json.loads(raw) if raw else None

    def state(self, entity_id: str) -> str:
        try:
            return self.request("/api/states/" + entity_id)["state"]
        except Exception as err:  # noqa: BLE001 - this is a diagnostic tool.
            return f"ERR:{type(err).__name__}:{err}"

    def call_service(self, domain: str, service: str, body: dict):
        result = self.request(f"/api/services/{domain}/{service}", "POST", body)
        changed = len(result) if isinstance(result, list) else "n/a"
        print(f"CALL {domain}.{service} {body} changed={changed}")


def snapshot(ha: HomeAssistant, label: str):
    print(f"\n[{time.strftime('%H:%M:%S')}] {label}")
    for entity in WATCH_ENTITIES:
        print(f"  {entity} => {ha.state(entity)}")


def set_switch(ha: HomeAssistant, entity_id: str, enabled: bool):
    ha.call_service("switch", "turn_on" if enabled else "turn_off", {"entity_id": entity_id})


def run_no_load(ha: HomeAssistant):
    snapshot(ha, "initial")
    power = "switch.chang_hong_ice_maker_esphome_power"
    large = "switch.chang_hong_ice_maker_esphome_large_ice"
    sequence = [
        (power, True),
        (large, False),
        (large, True),
        (power, False),
        (power, True),
        (large, False),
        (power, False),
        (large, True),
        (large, False),
        (power, True),
    ]
    for entity_id, enabled in sequence:
        set_switch(ha, entity_id, enabled)
        time.sleep(0.25)
    snapshot(ha, "after rapid switches")

    for _ in range(5):
        ha.call_service("button", "press", {"entity_id": "button.chang_hong_ice_maker_esphome_uv_toggle"})
        time.sleep(0.2)
    snapshot(ha, "during uv spam")

    time.sleep(1.0)
    set_switch(ha, power, True)
    set_switch(ha, large, False)
    snapshot(ha, "switches during uv")

    time.sleep(7.0)
    snapshot(ha, "after uv cooldown")
    ha.call_service("button", "press", {"entity_id": "button.chang_hong_ice_maker_esphome_uv_toggle"})
    time.sleep(1.3)
    snapshot(ha, "second uv accepted")
    time.sleep(7.0)
    snapshot(ha, "final")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ha-url", default="http://127.0.0.1:8123")
    parser.add_argument("--client-id", default="http://localhost:8123/")
    parser.add_argument("--auth-json", default=".ha-ice-maker-test/codex_auth.json")
    parser.add_argument("--scenario", choices=["no-load"], default="no-load")
    args = parser.parse_args()

    ha = HomeAssistant(args.ha_url, pathlib.Path(args.auth_json), args.client_id)
    if args.scenario == "no-load":
        run_no_load(ha)


if __name__ == "__main__":
    main()
