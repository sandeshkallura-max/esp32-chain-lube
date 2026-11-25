# Open Chain Bike – EmailJS support report setup

The website now has a polished **Report a Problem** page and sends a structured support request with the user's message, latest ESP32 diagnostics and optional location.

## 1. Reuse your existing “Contact Us” EmailJS template

You do **not** need to create a separate EmailJS template. Open the existing **Contact Us** template shown in your screenshot and change its fields as follows:

| EmailJS field | Set it to |
|---|---|
| To Email | `{{to_email}}` |
| From Name | `Open Chain Bike Website` |
| From Email | Keep **Use Default Email Address** checked |
| Reply To | Leave blank (the website does not collect a visitor email address) |
| Subject | `Open Chain Bike – Support Request: {{issue_type}}` |
| Bcc / Cc | Leave blank unless you intentionally want extra recipients |

The website supplies `to_email` as `s79178917@gmail.com`, so the report goes to the admin inbox.

## 2. Replace the Contact Us message body

In the **Content** tab, open the message/body editor. Switch to its HTML/source/code mode if available. Replace the old Contact Us body with the complete contents of `EMAILJS_TEMPLATE.html` in this project folder. Save the template.

The HTML creates a responsive, branded email with an Open Chain Bike header and separate sections for issue category, user message, ESP32 diagnostics and optional location. If you prefer the rich-text editor, use these exact variables as plain text instead:

```text
OPEN CHAIN BIKE
SMART CHAIN LUBRICATION · ESP32 MONITORING

SUPPORT REQUEST
Issue category: {{issue_type}}

MESSAGE FROM WEBSITE USER
{{user_message}}

ESP32 SYSTEM DIAGNOSTICS
{{system_diagnostics}}

USER LOCATION
{{user_location}}
```

Do not use the old Contact Us placeholders `{{title}}`, `{{name}}`, `{{email}}`, `{{message}}`, or `{{time}}`; this website does not send those fields. It sends `to_email`, `issue_type`, `user_message`, `system_diagnostics`, and `user_location`.

## 3. Copy the Template ID into the website

After saving the template, copy its Template ID from EmailJS. Open `data/index.html` and replace `template_fw1dc7g` in this line:

```javascript
const EMAILJS_TEMPLATE_ID='template_fw1dc7g';
```

Keep the single quotes. The other EmailJS settings are already filled in:

- Public Key: `Qh-XlHZ5neSWK8lIo`
- Service ID: `service_ivi9hjo`
- Admin recipient: `s79178917@gmail.com`

## 4. Upload the updated website to ESP32

1. Extract this ZIP.
2. Open the `esp32_chain_lubrication` folder (the folder containing `platformio.ini`) in Visual Studio Code with PlatformIO installed.
3. Connect the ESP32 over USB.
4. If only the website or template ID changed, run **PlatformIO: Upload Filesystem Image**. You do not need to re-upload firmware for HTML-only changes.
5. Open Serial Monitor at `115200` baud and reload the ESP32 dashboard.
6. Submit a test report and check the admin inbox and spam folder.

Terminal command from the project folder:

```bash
pio run -t uploadfs
```

For first-time setup or firmware changes, build and upload firmware first using **PlatformIO: Upload** or `pio run -t upload`, then upload the filesystem image.

## Important

- EmailJS template variables are populated from the parameter object sent by the website. Keep the spelling and underscores exactly as shown. See the [EmailJS dynamic variables guide](https://www.emailjs.com/docs/user-guide/dynamic-variables-templates/).
- The website loads the EmailJS browser SDK from a CDN, so the phone/browser needs internet access while sending. A phone connected to the ESP32 access point may not have internet at the same time.
- Location is optional. Browser geolocation may be blocked on the ESP32 local HTTP address; the report still sends with `Not shared / browser permission unavailable`.
- Never put your Gmail password, SMTP password or EmailJS Private Key in the website or firmware. The EmailJS Public Key is intended for client-side use. Configure origin restrictions and rate limits where available.
- This project has not been live-tested against your EmailJS account or physical ESP32 hardware.


## Troubleshooting: “The service ID not found”
This response comes from EmailJS, before the message is delivered. Open EmailJS Dashboard → Email Services, open the connected mail service, and copy its exact Service ID. Compare it character-for-character with `EMAILJS_SERVICE_ID` in `data/index.html`. If the service was deleted, disconnected, or belongs to a different EmailJS account, reconnect/create a service and replace the ID in the file. Save and upload the filesystem again (`pio run -t uploadfs`). The website cannot determine or repair an EmailJS dashboard service ID on its own.
