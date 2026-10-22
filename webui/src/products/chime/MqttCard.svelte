<script lang="ts">
  import MessageBanner from "../../components/MessageBanner.svelte";
  import { CORE_CONFIG_DEFAULTS, CORE_CONFIG_INT_BOUNDS } from "../../generated/config_schema";

  export let mqttHost: string;
  export let mqttPort: number;
  export let mqttClientId: string;
  export let mqttUsername: string;
  export let mqttPassword: string;
  export let mqttPasswordSet: boolean;
  export let mqttTlsEnabled: boolean;
  export let mqttTlsValidateCertificate: boolean;
  export let mqttTlsCaFile: string;
  export let mqttTlsCertFile: string;
  export let mqttTlsKeyFile: string;
  export let ringTopic: string;
  export let mqttTopics: string;
  export let isSaving: boolean;
  export let saveDisabled: boolean;
  export let messageText: string;
  export let messageIsError: boolean;
  export let observedTopics: string[];
  export let onRefreshTopics: () => Promise<void>;
  export let onSave: () => Promise<void>;
  export let showRetry = false;
  export let onRetry: () => void = () => {};
</script>

<section class="card">
  <h2>MQTT</h2>
  <div class="row">
    <div>
      <label for="mqtt_host">Host</label>
      <input id="mqtt_host" bind:value={mqttHost} placeholder="broker.local" />
    </div>
    <div>
      <label for="mqtt_port">Port</label>
      <input
        id="mqtt_port"
        type="number"
        min={CORE_CONFIG_INT_BOUNDS.mqtt_port.min}
        max={CORE_CONFIG_INT_BOUNDS.mqtt_port.max}
        bind:value={mqttPort}
      />
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
      <input
        id="ring_topic"
        bind:value={ringTopic}
        list="observed_topics"
        placeholder={CORE_CONFIG_DEFAULTS.ring_topic}
      />
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
      on:click={onRefreshTopics}
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
  <input id="mqtt_topics" bind:value={mqttTopics} placeholder="ring/+/pressed/#,ring/+/status" />

  <div class="button-row">
    {#if showRetry}
      <button class="secondary" type="button" on:click={onRetry}>Retry</button>
    {/if}
    <button
      type="button"
      disabled={saveDisabled}
      on:click={onSave}
    >
      Save &amp; Apply
    </button>
  </div>

  <MessageBanner text={messageText} isError={messageIsError} showSpinner={isSaving} />
</section>
