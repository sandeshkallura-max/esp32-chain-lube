# ESP32 Open Chain Bike – Smart Lubrication System

## Included
- `src/main.cpp`: ESP32 firmware, HTTP API, Hall RPM, DHT11, two-pump sequence, manual start and emergency stop.
- `data/index.html`: professionally refreshed, responsive engineering dashboard hosted directly by ESP32 (light enterprise theme, mobile navigation, sensor cards, charts, controls and support pages), including Dashboard, Monthly Report, Settings, Help and a polished Report a Problem page.
- `EMAILJS_TEMPLATE.html`: branded HTML email body to paste into the existing EmailJS Contact Us template.
- `EMAIL_SETUP.md`: exact EmailJS field settings and upload instructions.
- `platformio.ini`: PlatformIO build configuration.

## Default ESP32 DevKit V1 wiring
| Device | ESP32 GPIO |
|---|---:|
| Hall sensor OUT | 27 |
| DHT11 DATA | 4 |
| New pump relay IN | 25 |
| Old pump relay IN | 26 |
| Sensor/relay GND | GND |
| Sensor supply | Follow module voltage requirements |

Use a suitable external supply and properly rated relay/MOSFET driver for the pumps. Do not power pumps from an ESP32 GPIO. Ensure common ground where required by the driver design. Add flyback protection for inductive DC loads if not already built into the driver.

## Important relay warning
The supplied Arduino sketch specified Active LOW, so this project defaults to `RELAY_ACTIVE_LOW = true`. You said your relay polarity is unknown. **Do not connect the pump load until you identify the relay input logic and test it with the pumps disconnected.** On boot, the firmware first writes the configured OFF level and then configures the output pins. If the relay is Active HIGH, change the constant to `false`, rebuild and upload. Verify both relay channels remain OFF during reset and startup.

## Wi-Fi and access
The ESP32 always starts a fallback Wi-Fi access point:
- SSID: `ChainLube-ESP32`
- Password: `chainlube123`
- Website: `http://192.168.4.1`

For home/router mode, edit `WIFI_SSID` and `WIFI_PASSWORD` near the top of `src/main.cpp`. The AP remains available as fallback. When connected to your router, the serial monitor prints the router IP too.

Weather lookup is performed by the browser using Open-Meteo and needs internet access. If you connect directly to the ESP32 AP, your phone may not have internet while connected; use router mode for convenient weather lookup. Local sensor automation and pump control do not depend on the weather service.

## Build and upload (PlatformIO)
1. Install Visual Studio Code and the PlatformIO IDE extension.
2. Open this project folder in VS Code.
3. Connect ESP32 DevKit V1 by USB and select the correct serial port.
4. Edit Wi-Fi credentials if using router mode.
5. Build and upload firmware: PlatformIO `Upload`.
6. Upload website files: PlatformIO `Upload Filesystem Image` (or terminal `pio run -t uploadfs`).
7. Open Serial Monitor at 115200 baud. Visit the printed IP address.

CLI commands:
```bash
pio run
pio run -t upload
pio run -t uploadfs
pio device monitor
```

## Logic
- One Hall pulse represents one wheel revolution because you specified one magnet.
- RPM is calculated over a 2-second window.
- Every 10 Hall pulses queues an automatic lubrication cycle.
- DHT11 temperature at or above the website setpoint queues a cycle.
- Temperature trigger rearms at setpoint minus 2 °C.
- New pump: 10 s; both pumps OFF: 10 s; old pump: 10 s.
- Manual Start runs the same sequence.
- Emergency Stop immediately commands both relay outputs OFF and cancels queued requests. Resume re-enables automatic operation.
- Completed lubrication count and rolling event history are saved in ESP32 Preferences/LittleFS.

## Notes / limitations to verify before real operation
- This is a prototype implementation. Test with pump loads disconnected first.
- Monthly counts are calculated from timestamped event history when NTP time is available through router Wi-Fi. In AP-only mode, the dashboard falls back to lifetime totals marked with `*` because the ESP32 has no reliable calendar time.
- Event history is a rolling maximum of 500 entries.
- The 3-day graph is the selected city's outdoor forecast, not three days of historical DHT11 measurements. DHT11 readings at each completed lubrication event are stored in the event history.
- DHT11 is a low-cost sensor; do not treat it as a precision safety instrument.
- RPM assumes exactly one Hall pulse per wheel revolution. If the sensor generates extra pulses or misses magnets, recalibrate.
- The code uses the original 10-pulse trigger threshold. Adjust `WHEEL_LIMIT` if your intended lubrication interval differs.
- There is no physical hardware feedback confirming that lubricant actually flowed; relay ON status only confirms the ESP32 command state.
- Add a physical emergency stop in the pump power/control circuit for real-world use. Website emergency stop depends on Wi-Fi and software.


## Help and Report a Problem
- Help opens Google Maps searches for motorcycle mechanics, two-wheeler service centres and chain-lubrication shops near the phone's location. No Maps API key is needed for these external search links.
- Report a Problem collects the issue type/message and live ESP32 diagnostics (connection, sensor state, RPM/Hall count, DHT11 values, setpoint, pump states/sequence, lubrication counts, last cycle, uptime and relay logic).
- Browser location is optional and requested only on submit. Geolocation generally requires a secure context; `http://192.168.4.1` may block access. The report still sends without location. Google Maps can request location independently.
- EmailJS is preconfigured with the supplied Public Key, Service ID and admin recipient. It will not send until you create an EmailJS template and replace `PASTE_TEMPLATE_ID_HERE` in `data/index.html` with its Template ID. Template variables: `to_email`, `issue_type`, `user_message`, `system_diagnostics`, `user_location`. Follow `EMAIL_SETUP.md`.
- Never put Gmail/SMTP passwords in the ESP32 firmware or dashboard. Use a backend mail relay for production.


## Support email integration
The Report a Problem page sends the issue category, user message, live ESP32 diagnostics and optional browser location through EmailJS. Finish the existing Contact Us template using `EMAIL_SETUP.md`, then add its Template ID to `data/index.html`. The EmailJS public key, service ID and admin recipient are preconfigured.


## Buy Chain marketplace page

The dashboard includes a Buy Chain page with Indian motorcycle brand/model/year selectors (Honda, Hero, Bajaj, TVS, Yamaha, Suzuki, Royal Enfield, KTM and Jawa), plus a custom model field. It builds Amazon India and Flipkart search links for a drive chain and chain-sprocket kit. Marketplace pages show current seller photos and prices; this static ESP32 dashboard does not scrape or guarantee live prices or product compatibility. Always verify model year, variant, chain pitch, link count and sprocket fitment with the seller before purchase.
