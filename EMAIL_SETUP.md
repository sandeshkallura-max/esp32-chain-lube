# EmailJS setup

1. Create an EmailJS account, add an email service and create a template.
2. Include template variables `to_email`, `issue_type`, `user_message`, `system_diagnostics`, and `user_location`.
3. In `data/index.html`, replace `YOUR_EMAILJS_PUBLIC_KEY`, `YOUR_EMAILJS_SERVICE_ID`, `YOUR_EMAILJS_TEMPLATE_ID`, and `YOUR_ADMIN_EMAIL@example.com`.
4. Set the template recipient to `to_email` if your EmailJS setup supports dynamic recipients.
5. Test from a phone.

The ESP32 local HTTP page may not be allowed to read geolocation because browsers generally require HTTPS. If blocked, location is marked unavailable and the report still sends. Google Maps handles its own location permission from the Help page. Never enter your email account password into this project.
