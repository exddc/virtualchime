#include "chime/webd_ui_assets.h"

namespace chime::webd {

const std::string& MainPageHtml() {
  static const std::string html = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Chime Web Console</title>
  <style>
    :root {
      --bg: #f2f4f7;
      --card: #ffffff;
      --text: #1f2937;
      --muted: #4b5563;
      --accent: #0f766e;
      --error: #b91c1c;
      --border: #d1d5db;
    }
    body {
      margin: 0;
      font-family: "Segoe UI", "Helvetica Neue", sans-serif;
      background: linear-gradient(180deg, #e5f3f1 0%, var(--bg) 55%);
      color: var(--text);
    }
    .wrap {
      max-width: 880px;
      margin: 32px auto;
      padding: 0 16px 32px;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 20px;
      margin-bottom: 18px;
      box-shadow: 0 6px 24px rgba(15, 118, 110, 0.08);
    }
    h1 { margin: 0 0 8px; font-size: 28px; }
    h2 { margin: 0 0 12px; font-size: 20px; }
    p { margin: 0 0 14px; color: var(--muted); }
    label { display: block; margin: 10px 0 6px; font-weight: 600; }
    input, select {
      width: 100%;
      box-sizing: border-box;
      padding: 10px;
      border: 1px solid var(--border);
      border-radius: 8px;
      font-size: 14px;
      background: #fff;
    }
    button {
      border: none;
      background: var(--accent);
      color: #fff;
      padding: 10px 14px;
      border-radius: 8px;
      font-weight: 600;
      cursor: pointer;
      margin-right: 8px;
    }
    button.secondary {
      background: #334155;
    }
    .row { display: grid; gap: 12px; grid-template-columns: repeat(2, 1fr); }
    .message {
      margin-top: 10px;
      padding: 10px;
      border-radius: 8px;
      background: #ecfeff;
      border: 1px solid #a5f3fc;
      color: #155e75;
      display: none;
      white-space: pre-wrap;
    }
    .error {
      background: #fef2f2;
      border-color: #fecaca;
      color: var(--error);
    }
    .hint {
      margin-top: 8px;
      color: var(--muted);
      font-size: 13px;
    }
    @media (max-width: 740px) {
      .row { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h1>Chime Web Console</h1>
      <p>Configure Wi-Fi and MQTT. Changes are applied automatically.</p>
    </div>

    <div class="card">
      <h2>Wi-Fi</h2>
      <div class="row">
        <div>
          <label for="wifi_ssid">SSID</label>
          <input id="wifi_ssid" placeholder="Network name" />
        </div>
        <div>
          <label for="wifi_password">Password</label>
          <input id="wifi_password" type="password" placeholder="Leave blank to keep current" />
        </div>
      </div>
      <div style="margin-top: 10px;">
        <button type="button" class="secondary" id="scan_btn">Scan Networks</button>
      </div>
      <label for="scan_results">Scan Results</label>
      <select id="scan_results">
        <option value="">No scan results yet</option>
      </select>
      <p class="hint">Selecting an SSID fills the field above.</p>
    </div>

    <div class="card">
      <h2>MQTT</h2>
      <div class="row">
        <div>
          <label for="mqtt_host">Host</label>
          <input id="mqtt_host" placeholder="broker.local" />
        </div>
        <div>
          <label for="mqtt_port">Port</label>
          <input id="mqtt_port" type="number" min="1" max="65535" />
        </div>
      </div>
      <div class="row">
        <div>
          <label for="mqtt_client_id">Client ID</label>
          <input id="mqtt_client_id" />
        </div>
        <div>
          <label for="ring_topic">Ring Topic</label>
          <input id="ring_topic" placeholder="doorbell/ring" />
        </div>
      </div>
      <label for="mqtt_topics">Subscribe Topics (comma-separated)</label>
      <input id="mqtt_topics" placeholder="doorbell/ring,doorbell/status" />
      <div style="margin-top: 12px;">
        <button type="button" id="save_btn">Save & Apply</button>
      </div>
      <div id="message" class="message"></div>
    </div>
  </div>

  <script>
    const msg = document.getElementById('message');

    function setMessage(text, isError = false) {
      msg.textContent = text;
      msg.style.display = 'block';
      if (isError) {
        msg.classList.add('error');
      } else {
        msg.classList.remove('error');
      }
    }

    async function loadConfig() {
      const response = await fetch('/api/v1/config/core');
      const data = await response.json();
      if (!response.ok) {
        throw new Error(data.error || 'Failed to load config');
      }

      document.getElementById('wifi_ssid').value = data.wifi_ssid || '';
      document.getElementById('mqtt_host').value = data.mqtt_host || '';
      document.getElementById('mqtt_port').value = data.mqtt_port || 1883;
      document.getElementById('mqtt_client_id').value = data.mqtt_client_id || 'chime';
      document.getElementById('ring_topic').value = data.ring_topic || 'doorbell/ring';
      document.getElementById('mqtt_topics').value = (data.mqtt_topics || []).join(',');

      const wifiHint = data.wifi_password_set
        ? 'Password is set. Leave blank to keep it unchanged.'
        : 'No saved password yet. Enter one before saving.';
      setMessage(wifiHint, false);
    }

    async function scanNetworks() {
      const select = document.getElementById('scan_results');
      select.innerHTML = '<option value="">Scanning...</option>';

      const response = await fetch('/api/v1/wifi/scan');
      const data = await response.json();
      if (!response.ok) {
        throw new Error(data.error || 'Scan failed');
      }

      select.innerHTML = '';
      if (!data.networks || data.networks.length === 0) {
        select.innerHTML = '<option value="">No networks found</option>';
        return;
      }

      select.innerHTML = '<option value="">Select SSID</option>';
      data.networks.forEach((entry) => {
        const option = document.createElement('option');
        option.value = entry.ssid;
        option.textContent = `${entry.ssid} (${entry.signal_dbm} dBm, ${entry.security})`;
        select.appendChild(option);
      });
    }

    function parseTopics(csv) {
      return csv
        .split(',')
        .map((s) => s.trim())
        .filter((s) => s.length > 0);
    }

    async function saveConfig() {
      const payload = {
        wifi_ssid: document.getElementById('wifi_ssid').value.trim(),
        wifi_password: document.getElementById('wifi_password').value,
        mqtt_host: document.getElementById('mqtt_host').value.trim(),
        mqtt_port: Number(document.getElementById('mqtt_port').value),
        mqtt_client_id: document.getElementById('mqtt_client_id').value.trim(),
        mqtt_topics: parseTopics(document.getElementById('mqtt_topics').value),
        ring_topic: document.getElementById('ring_topic').value.trim(),
      };

      const response = await fetch('/api/v1/config/core', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      const data = await response.json();

      if (!response.ok) {
        if (data.validation_errors) {
          const details = data.validation_errors
            .map((e) => `${e.field}: ${e.message}`)
            .join('\n');
          throw new Error(details);
        }
        throw new Error(data.error || 'Save failed');
      }

      document.getElementById('wifi_password').value = '';
      setMessage(`Saved. Apply job ${data.apply.job_id} is ${data.apply.state}.`);
    }

    document.getElementById('scan_btn').addEventListener('click', async () => {
      try {
        await scanNetworks();
      } catch (error) {
        setMessage(error.message || String(error), true);
      }
    });

    document.getElementById('scan_results').addEventListener('change', (event) => {
      if (event.target.value) {
        document.getElementById('wifi_ssid').value = event.target.value;
      }
    });

    document.getElementById('save_btn').addEventListener('click', async () => {
      try {
        await saveConfig();
      } catch (error) {
        setMessage(error.message || String(error), true);
      }
    });

    loadConfig().catch((error) => setMessage(error.message || String(error), true));
  </script>
</body>
</html>
)HTML";

  return html;
}

}  // namespace chime::webd
