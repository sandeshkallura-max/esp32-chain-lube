# ESP32 Open Chain Bike – Smart Lubrication System (v2.0.0)

Automatic chain lubrication using an ESP32, Hall-effect wheel sensor, DHT11 and two relay-driven pumps, with a built-in web dashboard.

## Project layout
| Path | Purpose |
|---|---|
| `src/main.cpp` | Firmware: sensors, pump sequence, HTTP API |
| `data/index.html` | Dashboard served from LittleFS (works fully offline) |
| `platformio.ini` | Build config (not included here; see requirements below) |

**Requirements:** ESP32 DevKit V1, `DHT sensor library`, `ArduinoJson` **6.x** (e.g. `bblanchon/ArduinoJson@^6.21`), `board_build.filesystem = littlefs`.

## Wiring (ESP32 DevKit V1)
| Device | GPIO |
|---|---:|
| Hall sensor OUT | 27 |
| DHT11 DATA | 4 |
| New pump relay IN | 25 |
| Old pump relay IN | 26 |

Power the pumps from a separate, correctly rated supply through a relay/MOSFET driver. Never drive a pump from a GPIO. Share ground with the driver and add flyback protection on inductive loads.

## Safety checklist (do this before connecting pumps)
1. **Relay polarity.** Default is `RELAY_ACTIVE_LOW = true`. Test with pumps disconnected; if the relay is ON at idle, set it to `false`.
2. **Boot-time state.** ESP32 pins 25/26 are high-impedance during reset, so firmware alone cannot hold the relays OFF. Fit a ~10 kΩ pull-up to 3.3 V on each relay input (Active LOW) or pull-down to GND (Active HIGH). Check both relays stay OFF through reset and power-up.
3. **Physical emergency stop.** Add one in the pump power circuit. The web stop depends on Wi-Fi and software.
4. Change `AP_PASSWORD` and `API_KEY` in `main.cpp` before real use.

## Build and upload
```bash
pio run
pio run -t upload       # firmware
pio run -t uploadfs     # website (data/ folder)
pio device monitor      # 115200 baud
```
Default access point: `ChainLube-ESP32` → `http://192.168.4.1`. For router mode set `WIFI_SSID` / `WIFI_PASSWORD`; the AP stays available as fallback and the router IP prints on the serial monitor.

## Security
- Start, resume and setpoint requests require the `X-Api-Key` header. The dashboard asks for the key once per browser session.
- Emergency stop is intentionally open so it can never be blocked by a lost key.
- Traffic is plain HTTP on a local network; do not expose the device to the internet.

## Operating logic
- One Hall pulse = one wheel revolution (one magnet). RPM uses the measured interval.
- Every `WHEEL_LIMIT` (10) pulses, or DHT11 ≥ setpoint (re-arms at setpoint − 2 °C), queues a cycle.
- Cycle: new pump 10 s → both OFF 10 s → old pump 10 s. Requests queued together are served by one cycle; requests arriving mid-cycle run afterwards.
- Emergency stop turns both outputs OFF, clears the queue and blocks new requests (including manual start) until **Resume**.
- Three consecutive DHT11 failures raise `SENSOR_ERROR`; temperature triggers are suspended and no stale value is shown.
- Task watchdog (10 s) reboots the ESP32 if the main loop hangs.
- Counters persist in NVS; the last 500 events persist in LittleFS (`/history.bin`).

## Changes from v1
- **Fixed:** the new-pump relay was never switched ON (only the old pump ran).
- **Fixed:** history was silently truncated; the JSON buffer was too small for 500 entries. Now stored in a compact binary file and streamed to the browser. Existing `history.json` data is not migrated.
- **Fixed:** wheel requests arriving during a cycle were discarded.
- **Fixed:** manual start no longer overrides an active emergency stop.
- **Fixed:** wheel count was saved only at rollover (always 0); now saved periodically.
- Wi-Fi no longer blocks boot; NTP starts whenever the router connects.
- Removed the external background image so the dashboard works with no internet.

## Known limitations
- DHT11 is a low-accuracy sensor; not a safety instrument.
- No flow feedback: relay ON confirms the command, not that lubricant moved.
- Monthly counts need NTP time (router mode); in AP-only mode lifetime totals are shown, marked `*`.
- The 3-day chart is an outdoor forecast from Open-Meteo, not DHT11 history.
- Firmware has not been compiled or tested on hardware in this review.
