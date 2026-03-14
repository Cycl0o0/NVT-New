# NVT

This custom integration uses the repo's backend API and supports both networks through the same endpoints:

- `/api/lines?network=bdx|tls`
- `/api/alerts?network=bdx|tls`
- `/api/stop-groups?network=bdx|tls`
- `/api/stop-groups/:key/passages?network=bdx|tls`

Each NVT config entry monitors one `network + stop group + line` combination and exposes:

- a `Selected line` sensor
- a `Selected direction` sensor
- a `Next passage` timestamp sensor
- a `Minutes until passage` sensor
- a `Line alerts` sensor
- an `Alert details` sensor
- an `All alerts` sensor
- a `Line vehicles` sensor
- a `Passage within threshold` binary sensor

The config flow prompts for the `API URL`, the network, the stop group, the line, the direction, the threshold, and the refresh interval.
The line step only shows lines currently served by the selected stop, and the selector includes the line type and colors when the backend provides them.
The threshold is a free positive integer, so the user can choose `5`, `10`, `20`, or any larger value.

`Alert details` now exposes the alert message/details first instead of the title. `All alerts` keeps the full list of alert details in entity attributes. `Line vehicles` exposes vehicle positions in attributes under `map_positions` when coordinates are available.

## Install

1. Build and run the backend from this repo:

```bash
make backend-run
```

`backend-run` prints the exact API URL to paste into the NVT config flow.

2. Copy `custom_components/nvt` into your Home Assistant config directory under `custom_components/`.
3. Restart Home Assistant.
4. Add the `NVT` integration from the UI.
5. Enter the API URL, choose `Bordeaux` or `Toulouse`, then select the stop, line, direction, and threshold you want to monitor.

## Automation example

```yaml
alias: Notify before passage
triggers:
  - trigger: state
    entity_id: binary_sensor.my_stop_passage_within_threshold
    to: "on"
actions:
  - action: notify.mobile_app_phone
    data:
      message: "Your selected line is about to pass."
```

Create multiple config entries if you want to monitor multiple lines, stops, or both networks.
