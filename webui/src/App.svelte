<script lang="ts">
  import { onMount } from "svelte";

  type ValidationError = {
    field: string;
    message: string;
  };

  type ApplyStatus = {
    job_id: number;
    state: string;
    started_at_utc?: string;
    finished_at_utc?: string;
    error?: string;
  };

  type CoreConfigResponse = {
    wifi_ssid?: string;
    wifi_password_set?: boolean;
    mqtt_host?: string;
    mqtt_port?: number;
    mqtt_client_id?: string;
    mqtt_username?: string;
    mqtt_password_set?: boolean;
    mqtt_tls_enabled?: boolean;
    mqtt_tls_validate_certificate?: boolean;
    mqtt_tls_ca_file?: string;
    mqtt_tls_cert_file?: string;
    mqtt_tls_key_file?: string;
    mqtt_topics?: string[];
    ring_topic?: string;
    volume_bell?: number;
    volume_notifications?: number;
    volume_other?: number;
    apply?: ApplyStatus;
    error?: string;
    message?: string;
  };

  type RingSoundsResponse = {
    sounds?: string[];
    selected_sound?: string;
    error?: string;
    message?: string;
  };

  type WifiNetwork = {
    ssid: string;
    signal_dbm: number;
    security: string;
  };

  type WifiScanResponse = {
    networks?: WifiNetwork[];
    error?: string;
    message?: string;
  };

  type ObservedTopicsResponse = {
    topics?: string[];
    error?: string;
    message?: string;
  };

  type SaveResponse = CoreConfigResponse & {
    validation_errors?: ValidationError[];
  };

  let wifiSsid = "";
  let wifiPassword = "";
  let mqttHost = "";
  let mqttPort = 1883;
  let mqttClientId = "chime";
  let mqttUsername = "";
  let mqttPassword = "";
  let mqttPasswordSet = false;
  let mqttTlsEnabled = false;
  let mqttTlsValidateCertificate = true;
  let mqttTlsCaFile = "";
  let mqttTlsCertFile = "";
  let mqttTlsKeyFile = "";
  let ringTopic = "doorbell/ring";
  let volumeBell = 80;
  let volumeNotifications = 70;
  let volumeOther = 70;
  let mqttTopics = "";
  let ringSounds: string[] = [];
  let selectedRingSound = "";
  let ringSoundUpload: File | null = null;
  let preparedUploadName = "";
  let isUploadingRingSound = false;

  let scanResults: WifiNetwork[] = [];
  let selectedScanSsid = "";
  let observedTopics: string[] = [];

  let messageText = "";
  let messageIsError = false;
  let isSaving = false;

  function setMessage(text: string, isError = false): void {
    messageText = text;
    messageIsError = isError;
  }

  function parseTopics(csv: string): string[] {
    return csv
      .split(",")
      .map((entry) => entry.trim())
      .filter((entry) => entry.length > 0);
  }

  function clampVolumeValue(value: unknown, fallback: number): number {
    const parsed = typeof value === "number" ? value : Number(value);
    if (!Number.isFinite(parsed)) {
      return fallback;
    }
    return Math.min(100, Math.max(0, Math.round(parsed)));
  }

  async function loadConfig(): Promise<void> {
    const response = await fetch("/api/v1/config/core");
    const data = (await response.json()) as CoreConfigResponse;

    if (!response.ok) {
      throw new Error(data.error ?? "Failed to load config");
    }

    wifiSsid = data.wifi_ssid ?? "";
    mqttHost = data.mqtt_host ?? "";
    mqttPort = data.mqtt_port ?? 1883;
    mqttClientId = data.mqtt_client_id ?? "chime";
    mqttUsername = data.mqtt_username ?? "";
    mqttPasswordSet = data.mqtt_password_set ?? false;
    mqttTlsEnabled = data.mqtt_tls_enabled ?? false;
    mqttTlsValidateCertificate = data.mqtt_tls_validate_certificate ?? true;
    mqttTlsCaFile = data.mqtt_tls_ca_file ?? "";
    mqttTlsCertFile = data.mqtt_tls_cert_file ?? "";
    mqttTlsKeyFile = data.mqtt_tls_key_file ?? "";
    ringTopic = data.ring_topic ?? "doorbell/ring";
    volumeBell = data.volume_bell ?? 80;
    volumeNotifications = data.volume_notifications ?? 70;
    volumeOther = data.volume_other ?? 70;
    mqttTopics = (data.mqtt_topics ?? []).join(",");

    const wifiHint = data.wifi_password_set
      ? "Password is set. Leave blank to keep it unchanged."
      : "No saved password yet. Enter one before saving.";
    setMessage(wifiHint, false);
  }

  function sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  async function loadApplyStatus(): Promise<ApplyStatus | undefined> {
    const response = await fetch("/api/v1/config/core");
    const data = (await response.json()) as CoreConfigResponse;
    if (!response.ok) {
      throw new Error(data.error ?? "Failed to load apply status");
    }
    return data.apply;
  }

  async function waitForApplyCompletion(jobId: number): Promise<void> {
    const timeoutMs = 90_000;
    const pollMs = 800;
    const startedAt = Date.now();
    let transientErrors = 0;

    while (Date.now() - startedAt < timeoutMs) {
      try {
        const apply = await loadApplyStatus();
        if (apply && apply.job_id === jobId) {
          if (apply.state === "succeeded") {
            return;
          }
          if (apply.state === "failed") {
            throw new Error(apply.error || "Apply failed");
          }
        }
      } catch (error) {
        transientErrors += 1;
        if (transientErrors >= 5) {
          throw error;
        }
      }
      await sleep(pollMs);
    }

    throw new Error("Timed out waiting for apply to complete.");
  }

  async function scanNetworks(): Promise<void> {
    const response = await fetch("/api/v1/wifi/scan");
    const data = (await response.json()) as WifiScanResponse;

    if (!response.ok) {
      throw new Error(data.error ?? "Scan failed");
    }

    scanResults = data.networks ?? [];
    if (scanResults.length === 0) {
      setMessage("No networks found.", false);
    }
  }

  async function loadObservedTopics(): Promise<void> {
    const response = await fetch("/api/v1/mqtt/topics");
    const data = (await response.json()) as ObservedTopicsResponse;

    if (!response.ok) {
      throw new Error(data.error ?? "Failed to load observed topics");
    }

    observedTopics = data.topics ?? [];
  }

  async function loadRingSounds(): Promise<void> {
    const response = await fetch("/api/v1/ring/sounds");
    const data = (await response.json()) as RingSoundsResponse;

    if (!response.ok) {
      throw new Error(data.error ?? "Failed to load ring sounds");
    }

    ringSounds = data.sounds ?? [];
    selectedRingSound = data.selected_sound ?? "";
  }


  function buildUploadSoundName(originalName: string): string {
    const lower = originalName.toLowerCase();
    const normalized = lower
      .replace(/[^a-z0-9_.-]+/g, "-")
      .replace(/[-._]{2,}/g, "-")
      .replace(/^[._-]+/, "")
      .replace(/[._-]+$/, "");

    const withExtension = normalized.endsWith(".wav")
      ? normalized
      : `${normalized}.wav`;

    const withoutPrefix = withExtension.replace(/^ring-/, "");
    const candidate = `ring-${withoutPrefix}`;

    const cleaned = candidate
      .replace(/[^a-z0-9_.-]+/g, "-")
      .replace(/[-._]{2,}/g, "-")
      .replace(/^[-._]+/, "")
      .replace(/[-._]+$/, "");

    if (!cleaned || cleaned === "ring" || cleaned === "ring.wav") {
      return "ring-custom.wav";
    }

    if (!cleaned.endsWith(".wav")) {
      return `${cleaned}.wav`;
    }

    return cleaned;
  }

  async function uploadRingSound(): Promise<void> {
    if (!ringSoundUpload) {
      throw new Error("Choose a .wav file to upload.");
    }

    const fileNameLower = ringSoundUpload.name.toLowerCase();
    const hasWavExtension = fileNameLower.endsWith(".wav");
    const hasWavMimeType =
      ringSoundUpload.type === "audio/wav" || ringSoundUpload.type === "audio/x-wav";
    if (!hasWavExtension && !hasWavMimeType) {
      isUploadingRingSound = false;
      throw new Error("Please select a .wav file.");
    }
    if (ringSoundUpload.size > 2 * 1024 * 1024) {
      isUploadingRingSound = false;
      throw new Error("File must be <= 2MB.");
    }

    const uploadName = buildUploadSoundName(ringSoundUpload.name);

    isUploadingRingSound = true;
    try {
      const response = await fetch(`/api/v1/ring/sounds/${encodeURIComponent(uploadName)}`, {
        method: "PUT",
        body: await ringSoundUpload.arrayBuffer(),
      });
      const data = (await response.json()) as { error?: string; message?: string };

      if (!response.ok) {
        throw new Error(data.message ?? data.error ?? "Upload failed");
      }

      await loadRingSounds();
      selectedRingSound = uploadName;
      setMessage(`Uploaded ${uploadName}. Select it below to activate.`, false);
    } finally {
      isUploadingRingSound = false;
    }
  }

  async function activateRingSound(): Promise<void> {
    if (!selectedRingSound) {
      throw new Error("Select a ring sound to activate.");
    }

    const response = await fetch("/api/v1/ring/sounds/select", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: selectedRingSound }),
    });
    const data = (await response.json()) as {
      error?: string;
      message?: string;
      selection_persisted?: boolean;
    };

    if (!response.ok) {
      throw new Error(data.message ?? data.error ?? "Failed to activate sound");
    }

    if (data.selection_persisted === false) {
      setMessage(
        "Ring sound activated, but selected-sound metadata could not be persisted.",
        true,
      );
      return;
    }

    setMessage("Ring sound updated. New rings use this sound immediately.", false);
  }

  async function saveConfig(): Promise<void> {
    isSaving = true;
    setMessage("Saving and applying changes...", false);

    const safeVolumeBell = clampVolumeValue(volumeBell, 80);
    const safeVolumeNotifications = clampVolumeValue(volumeNotifications, 70);
    const safeVolumeOther = clampVolumeValue(volumeOther, 70);

    volumeBell = safeVolumeBell;
    volumeNotifications = safeVolumeNotifications;
    volumeOther = safeVolumeOther;

    const payload = {
      wifi_ssid: wifiSsid.trim(),
      wifi_password: wifiPassword,
      mqtt_host: mqttHost.trim(),
      mqtt_port: Number(mqttPort),
      mqtt_client_id: mqttClientId.trim(),
      mqtt_username: mqttUsername.trim(),
      mqtt_password: mqttPassword,
      mqtt_tls_enabled: mqttTlsEnabled,
      mqtt_tls_validate_certificate: mqttTlsValidateCertificate,
      mqtt_tls_ca_file: mqttTlsCaFile.trim(),
      mqtt_tls_cert_file: mqttTlsCertFile.trim(),
      mqtt_tls_key_file: mqttTlsKeyFile.trim(),
      mqtt_topics: parseTopics(mqttTopics),
      ring_topic: ringTopic.trim(),
      volume_bell: safeVolumeBell,
      volume_notifications: safeVolumeNotifications,
      volume_other: safeVolumeOther,
    };

    try {
      const response = await fetch("/api/v1/config/core", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      const data = (await response.json()) as SaveResponse;

      if (!response.ok) {
        if (data.validation_errors && data.validation_errors.length > 0) {
          const details = data.validation_errors
            .map((entry) => `${entry.field}: ${entry.message}`)
            .join("\n");
          throw new Error(details);
        }
        throw new Error(data.error ?? "Save failed");
      }

      wifiPassword = "";
      mqttPassword = "";
      mqttPasswordSet = data.mqtt_password_set ?? mqttPasswordSet;

      const apply = data.apply;
      if (!apply || apply.state === "succeeded") {
        setMessage("Saved and applied.", false);
        return;
      }

      await waitForApplyCompletion(apply.job_id);
      setMessage("Saved and applied.", false);
    } finally {
      isSaving = false;
    }
  }

  function onScanSelectionChanged(event: Event): void {
    const target = event.currentTarget as HTMLSelectElement;
    selectedScanSsid = target.value;
    if (selectedScanSsid) {
      wifiSsid = selectedScanSsid;
    }
  }

  onMount(() => {
    loadConfig()
      .then(async () => {
        await Promise.all([loadObservedTopics(), loadRingSounds()]);
      })
      .catch((error: unknown) => {
        const text = error instanceof Error ? error.message : String(error);
        setMessage(text, true);
      });
  });
</script>

<div class="wrap">
  <section class="card">
    <h1>Chime Web Console</h1>
    <p>Configure Wi-Fi and MQTT. Changes are applied automatically.</p>
  </section>

  <section class="card">
    <h2>Wi-Fi</h2>
    <div class="row">
      <div>
        <label for="wifi_ssid">SSID</label>
        <input id="wifi_ssid" bind:value={wifiSsid} placeholder="Network name" />
      </div>
      <div>
        <label for="wifi_password">Password</label>
        <input
          id="wifi_password"
          type="password"
          bind:value={wifiPassword}
          placeholder="Leave blank to keep current"
        />
      </div>
    </div>

    <div class="button-row">
      <button
        class="secondary"
        type="button"
        disabled={isSaving}
        on:click={async () => {
          try {
            await scanNetworks();
          } catch (error) {
            setMessage(error instanceof Error ? error.message : String(error), true);
          }
        }}
      >
        Scan Networks
      </button>
    </div>

    <label for="scan_results">Scan Results</label>
    <select id="scan_results" bind:value={selectedScanSsid} on:change={onScanSelectionChanged}>
      <option value="">Select SSID</option>
      {#if scanResults.length === 0}
        <option value="" disabled>No scan results yet</option>
      {/if}
      {#each scanResults as network}
        <option value={network.ssid}>
          {network.ssid} ({network.signal_dbm} dBm, {network.security})
        </option>
      {/each}
    </select>
    <p class="hint">Selecting an SSID fills the field above.</p>
  </section>

  <section class="card">
    <h2>Ring Sound</h2>
    <div class="row">
      <div>
        <label for="ring_sound_upload">Upload WAV</label>
        <input
          id="ring_sound_upload"
          type="file"
          accept=".wav,audio/wav"
          on:change={(event) => {
            const target = event.currentTarget as HTMLInputElement;
            ringSoundUpload = target.files && target.files.length > 0 ? target.files[0] : null;
            preparedUploadName = ringSoundUpload ? buildUploadSoundName(ringSoundUpload.name) : "";
          }}
        />
      </div>
      <div>
        <label for="ring_sound_select">Available Sounds</label>
        <select id="ring_sound_select" bind:value={selectedRingSound}>
          <option value="">Select sound</option>
          {#each ringSounds as sound}
            <option value={sound}>{sound}</option>
          {/each}
        </select>
      </div>
    </div>
    {#if preparedUploadName}
      <p class="hint">Upload filename: <code>{preparedUploadName}</code></p>
    {/if}
    <div class="button-row">
      <button
        class="secondary"
        type="button"
        disabled={isUploadingRingSound || !ringSoundUpload}
        on:click={async () => {
          try {
            await uploadRingSound();
          } catch (error) {
            setMessage(error instanceof Error ? error.message : String(error), true);
          }
        }}
      >
        Upload Sound
      </button>
      <button
        type="button"
        disabled={isUploadingRingSound || !selectedRingSound}
        on:click={async () => {
          try {
            await activateRingSound();
          } catch (error) {
            setMessage(error instanceof Error ? error.message : String(error), true);
          }
        }}
      >
        Activate Selected Sound
      </button>
      <button
        class="secondary"
        type="button"
        on:click={async () => {
          try {
            await loadRingSounds();
            setMessage("Ring sounds refreshed.", false);
          } catch (error) {
            setMessage(error instanceof Error ? error.message : String(error), true);
          }
        }}
      >
        Refresh Sounds
      </button>
    </div>
    <p class="hint">Upload a file and activate it. The chime daemon will use it for new rings without a restart.</p>
  </section>

  <section class="card">
    <h2>Volume</h2>
    <div class="row">
      <div>
        <label for="volume_bell">Bell (%)</label>
        <input id="volume_bell" type="number" min="0" max="100" bind:value={volumeBell} />
      </div>
      <div>
        <label for="volume_notifications">Notifications (%)</label>
        <input
          id="volume_notifications"
          type="number"
          min="0"
          max="100"
          bind:value={volumeNotifications}
        />
      </div>
    </div>
    <div class="row">
      <div>
        <label for="volume_other">Other (%)</label>
        <input id="volume_other" type="number" min="0" max="100" bind:value={volumeOther} />
      </div>
      <div></div>
    </div>
    <p class="hint">These are software volume levels (0-100) applied before playback.</p>
  </section>

  <section class="card">
    <h2>MQTT</h2>
    <div class="row">
      <div>
        <label for="mqtt_host">Host</label>
        <input id="mqtt_host" bind:value={mqttHost} placeholder="broker.local" />
      </div>
      <div>
        <label for="mqtt_port">Port</label>
        <input id="mqtt_port" type="number" min="1" max="65535" bind:value={mqttPort} />
      </div>
    </div>

    <div class="row">
      <div>
        <label for="mqtt_client_id">Client ID</label>
        <input id="mqtt_client_id" bind:value={mqttClientId} />
      </div>
      <div>
        <label for="mqtt_username">Username</label>
        <input id="mqtt_username" bind:value={mqttUsername} placeholder="Optional" />
      </div>
    </div>

    <div class="row">
      <div>
        <label for="mqtt_password">Password</label>
        <input
          id="mqtt_password"
          type="password"
          bind:value={mqttPassword}
          placeholder="Leave blank to keep current"
        />
      </div>
      <div>
        <label for="ring_topic">Ring Topic</label>
        <input id="ring_topic" bind:value={ringTopic} list="observed_topics" placeholder="doorbell/ring" />
        <datalist id="observed_topics">
          {#each observedTopics as topic}
            <option value={topic}></option>
          {/each}
        </datalist>
      </div>
    </div>
    <div class="button-row">
      <button
        class="secondary"
        type="button"
        on:click={async () => {
          try {
            await loadObservedTopics();
            setMessage("Observed topics refreshed.");
          } catch (error) {
            setMessage(error instanceof Error ? error.message : String(error), true);
          }
        }}
      >
        Refresh Observed Topics
      </button>
    </div>
    <p class="hint">Use suggestions or enter a topic manually.</p>
    <p class="hint">
      {mqttPasswordSet
        ? "MQTT password is set. Leave blank to keep it unchanged."
        : "No MQTT password saved yet."}
    </p>
    <div class="row">
      <div>
        <label for="mqtt_tls_enabled">TLS Enabled</label>
        <input id="mqtt_tls_enabled" type="checkbox" bind:checked={mqttTlsEnabled} />
      </div>
      <div>
        <label for="mqtt_tls_validate_certificate">Validate Certificate</label>
        <input
          id="mqtt_tls_validate_certificate"
          type="checkbox"
          bind:checked={mqttTlsValidateCertificate}
        />
      </div>
    </div>

    <div class="row">
      <div>
        <label for="mqtt_tls_ca_file">CA File</label>
        <input id="mqtt_tls_ca_file" bind:value={mqttTlsCaFile} placeholder="/etc/ssl/certs/ca.pem" />
      </div>
      <div>
        <label for="mqtt_tls_cert_file">Client Cert File</label>
        <input id="mqtt_tls_cert_file" bind:value={mqttTlsCertFile} placeholder="/etc/chime/client.crt" />
      </div>
    </div>

    <div class="row">
      <div>
        <label for="mqtt_tls_key_file">Client Key File</label>
        <input id="mqtt_tls_key_file" bind:value={mqttTlsKeyFile} placeholder="/etc/chime/client.key" />
      </div>
      <div></div>
    </div>
    <p class="hint">Client cert/key are optional, but must be provided together.</p>

    <label for="mqtt_topics">Subscribe Topics (comma-separated)</label>
    <input id="mqtt_topics" bind:value={mqttTopics} placeholder="doorbell/ring,doorbell/status" />

    <div class="button-row">
      <button
        type="button"
        disabled={isSaving}
        on:click={async () => {
          try {
            await saveConfig();
          } catch (error) {
            setMessage(error instanceof Error ? error.message : String(error), true);
          }
        }}
      >
        Save &amp; Apply
      </button>
    </div>

    {#if messageText}
      <div class:error={messageIsError} class="message">
        {#if isSaving}
          <span class="spinner" aria-hidden="true"></span>
        {/if}
        {messageText}
      </div>
    {/if}
  </section>
</div>
