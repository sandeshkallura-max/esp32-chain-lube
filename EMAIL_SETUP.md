# EmailJS setup – configured values

The dashboard has been preconfigured with the supplied EmailJS values:

- Public Key: `Qh-XlHZ5neSWK8lIo`
- Service ID: `service_l2c9xo5`
- Admin recipient: `s79178917@gmail.com`
- Template ID: **not supplied yet**. The dashboard currently contains `PASTE_TEMPLATE_ID_HERE` as a placeholder, so sending is intentionally blocked until you replace it.

## 1. Create the EmailJS template

1. Sign in at https://dashboard.emailjs.com/ and open **Email Templates**.
2. Choose **Create New Template**. Give it a name such as `ESP32 Chain Lubrication Problem Report`.
3. Set **To Email** to `{{to_email}}`. The dashboard code supplies `s79178917@gmail.com` in this parameter.
4. Set the subject to `ESP32 Chain Lubrication – Problem Report: {{issue_type}}`.
5. Set the email content to the following (plain text is fine):

```text
ESP32 CHAIN LUBRICATION – PROBLEM REPORT

Issue type:
{{issue_type}}

User message:
{{user_message}}

System diagnostics:
{{system_diagnostics}}

User location:
{{user_location}}
```

6. Save the template and copy its **Template ID** (often shown as `template_...`).

## 2. Add the Template ID to the project

Open `data/index.html` and find this line:

```javascript
const EMAILJS_TEMPLATE_ID='PASTE_TEMPLATE_ID_HERE';
```

Replace only `PASTE_TEMPLATE_ID_HERE` with your actual Template ID, keeping the single quotes. Save the file. No other EmailJS values need changing.

## 3. Upload the dashboard files to ESP32

In VS Code with PlatformIO:

1. Open the `esp32_chain_lubrication` folder (the folder containing `platformio.ini`).
2. Connect the ESP32 by USB.
3. Run **PlatformIO: Build** and **PlatformIO: Upload** if you have changed firmware or are setting it up for the first time.
4. Run **PlatformIO: Upload Filesystem Image** to upload `data/index.html` to the ESP32. This step is essential for dashboard changes.
5. Open the Serial Monitor at `115200` baud and visit the ESP32 IP address. Refresh the dashboard, then test **Report a Problem**.

CLI alternative from the project folder:

```bash
pio run -t uploadfs
```

If firmware is also required, run `pio run -t upload` before `pio run -t uploadfs`.

## Important notes

- The ESP32-hosted dashboard loads the EmailJS browser SDK from a CDN, so the phone/browser needs internet access while submitting the report. A phone connected to the ESP32 access point may not have internet unless it also has a suitable internet path.
- Browser geolocation may be blocked on `http://192.168.4.1`; location is optional and the report should still send without it.
- Never place your Gmail password, SMTP password, or EmailJS Private Key in the dashboard or firmware. The Public Key is intended for client-side use. Configure EmailJS origin restrictions/rate limits where available.
- The email integration has not been live-tested against your EmailJS account. After creating the template, use EmailJS's test function and then submit a test report from the dashboard.
