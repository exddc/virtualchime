export const CONFIG_SCHEMA_VERSION = 7 as const;

export type CoreConfigFields = {
  mqtt_host: string;
  mqtt_port: number;
  mqtt_client_id: string;
  mqtt_username: string;
  mqtt_tls_enabled: boolean;
  mqtt_tls_validate_certificate: boolean;
  mqtt_tls_ca_file: string;
  mqtt_tls_cert_file: string;
  mqtt_tls_key_file: string;
  mqtt_topics: string[];
  ring_topic: string;
  notification_success_sound_path?: string;
  notification_failure_sound_path?: string;
  volume_ring: number;
  volume_notifications: number;
  wifi_ssid: string;
};

export const CORE_CONFIG_DEFAULTS: CoreConfigFields = {
  mqtt_host: "",
  mqtt_port: 1883,
  mqtt_client_id: "chime",
  mqtt_username: "",
  mqtt_tls_enabled: false,
  mqtt_tls_validate_certificate: true,
  mqtt_tls_ca_file: "",
  mqtt_tls_cert_file: "",
  mqtt_tls_key_file: "",
  mqtt_topics: [],
  ring_topic: "ring/+/pressed/#",
  notification_success_sound_path: "/usr/local/share/chime/test.wav",
  notification_failure_sound_path: "/usr/local/share/chime/ring.wav",
  volume_ring: 80,
  volume_notifications: 70,
  wifi_ssid: "",
};

export const CORE_CONFIG_INT_BOUNDS = {
  mqtt_port: { min: 1, max: 65535 },
  volume_ring: { min: 0, max: 100 },
  volume_notifications: { min: 0, max: 100 },
} as const;
