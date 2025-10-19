async function fetchConfig() {
  try {
    const res = await fetch('/api/config', {cache: 'no-store'});
    if (!res.ok) {
      showStatus('Erreur chargement config: ' + res.status, true);
      return;
    }
    const json = await res.json();
    // Populate fields
    document.getElementById('mqtt_enabled').checked = json.mqtt_enabled === true;
    document.getElementById('mqtt_host').value = json.mqtt_host || '';
    document.getElementById('mqtt_port').value = json.mqtt_port || 1883;
    document.getElementById('mqtt_user').value = json.mqtt_user || '';
    // don't fill mqtt_pass if masked; attempt to leave empty so user can change if needed
    document.getElementById('mqtt_topic').value = json.mqtt_topic || '';

    document.getElementById('measure_interval_ms').value = json.measure_interval_ms || 200;
    document.getElementById('measure_offset_cm').value = json.measure_offset_cm || 0;

    // NEW: stabilisation / filtre bruit
    document.getElementById('avg_alpha').value = (typeof json.avg_alpha === 'number') ? json.avg_alpha : 0.25;
    document.getElementById('median_n').value = json.median_n || 5;
    document.getElementById('median_delay_ms').value = json.median_delay_ms || 50;
    document.getElementById('filter_min_cm').value = (typeof json.filter_min_cm === 'number') ? json.filter_min_cm : 2.0;
    document.getElementById('filter_max_cm').value = (typeof json.filter_max_cm === 'number') ? json.filter_max_cm : 400.0;

    document.getElementById('device_name').value = json.device_name || '';
    document.getElementById('interactive_timeout_ms').value = json.interactive_timeout_ms || 60000;
    document.getElementById('deepsleep_interval_s').value = json.deepsleep_interval_s || 30;

    document.getElementById('admin_user').value = json.admin_user || '';
    // admin_pass left empty (masked)
    showStatus('Config chargée', false);
  } catch (e) {
    showStatus('Erreur fetch: ' + e, true);
  }
}

function gatherConfig() {
  const obj = {};
  obj.mqtt_enabled = document.getElementById('mqtt_enabled').checked;
  obj.mqtt_host = document.getElementById('mqtt_host').value;
  obj.mqtt_port = parseInt(document.getElementById('mqtt_port').value) || 1883;
  obj.mqtt_user = document.getElementById('mqtt_user').value;
  const mp = document.getElementById('mqtt_pass').value;
  if (mp && mp.length > 0) obj.mqtt_pass = mp;
  obj.mqtt_topic = document.getElementById('mqtt_topic').value;

  obj.measure_interval_ms = parseInt(document.getElementById('measure_interval_ms').value) || 200;
  obj.measure_offset_cm = parseFloat(document.getElementById('measure_offset_cm').value) || 0.0;

  // NEW: stabilisation / filtre bruit
  obj.avg_alpha = Math.max(0, Math.min(1, parseFloat(document.getElementById('avg_alpha').value)));
  obj.median_n = Math.max(1, Math.min(15, parseInt(document.getElementById('median_n').value) || 5));
  obj.median_delay_ms = Math.max(0, Math.min(1000, parseInt(document.getElementById('median_delay_ms').value) || 50));
  obj.filter_min_cm = parseFloat(document.getElementById('filter_min_cm').value);
  obj.filter_max_cm = parseFloat(document.getElementById('filter_max_cm').value);

  obj.device_name = document.getElementById('device_name').value || '';
  obj.interactive_timeout_ms = parseInt(document.getElementById('interactive_timeout_ms').value) || 60000;
  obj.deepsleep_interval_s = parseInt(document.getElementById('deepsleep_interval_s').value) || 30;

  obj.admin_user = document.getElementById('admin_user').value || '';
  const ap = document.getElementById('admin_pass').value;
  if (ap && ap.length > 0) obj.admin_pass = ap; // only include if user provided a new pass

  return obj;
}

async function saveConfig() {
  const payload = gatherConfig();
  try {
    const res = await fetch('/api/config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(payload)
    });
    const j = await res.json();
    if (res.ok && j.ok) {
      showStatus('Config sauvegardée', false);
      // Clear password field after saving
      document.getElementById('admin_pass').value = '';
      document.getElementById('mqtt_pass').value = '';
    } else {
      showStatus('Erreur sauvegarde', true);
    }
  } catch (e) {
    showStatus('Erreur POST: ' + e, true);
  }
}

function showStatus(msg, isError) {
  const el = document.getElementById('status');
  el.innerText = msg;
  el.style.color = isError ? 'red' : 'green';
}

document.getElementById('btnSave').addEventListener('click', () => {
  saveConfig();
});

document.getElementById('btnReload').addEventListener('click', () => {
  fetchConfig();
});

function sendConfigPing() {
  fetch('/ping', {
    method: 'POST',
    body: new URLSearchParams({page: 'config'})
  });
}
setInterval(sendConfigPing, 10000);

// initial load
fetchConfig();
