# Website password protection

The ESP32 web server now protects the dashboard and all registered API routes with HTTP Basic Authentication.

- Browser username: `admin`
- Website password: `AutoLube`
- The browser may remember the login until its authentication session is cleared.

## Upload
1. Open this project in PlatformIO.
2. Build and upload the firmware: `pio run -t upload`.
3. Upload the website filesystem: `pio run -t uploadfs` (only if website files were changed; this login change is in firmware only).
4. Connect to `ChainLube-ESP32` and open `http://192.168.4.1`.
5. When prompted, enter username `admin` and password `AutoLube`.

## Security note
This is HTTP Basic Authentication over plain HTTP. It is suitable as a simple access gate on a trusted local network, but it does not encrypt credentials in transit. Do not expose the ESP32 web server directly to the public internet. Change the default ESP32 access-point password before deployment and use a trusted network.
