/**
 * Shared MQTT publish helper for edge functions.
 *
 * Uses npm:mqtt@5 over WebSocket (ws://) because the Supabase Edge Runtime
 * sandboxes Deno.connect() — raw TCP is blocked, but WebSocket (HTTP upgrade)
 * is allowed. Mosquitto listens on port 9001 for WebSocket connections.
 */
import mqtt from 'npm:mqtt@5'

const MQTT_CONNECT_TIMEOUT_MS = 5_000

/**
 * Connect to the MQTT broker via WebSocket, publish a message, and disconnect.
 * Throws on connection failure or timeout.
 */
export async function mqttPublish(
  topic: string,
  payload: string | Uint8Array,
  options?: { qos?: 0 | 1 | 2 },
): Promise<void> {
  const host = Deno.env.get('MQTT_HOST') ?? 'broker'
  const wsPort = Deno.env.get('MQTT_WS_PORT') ?? '9001'
  const user = Deno.env.get('MQTT_ADMIN_USER') ?? 'admin'
  const pass = Deno.env.get('MQTT_ADMIN_PASS') ?? 'admin'

  const client = mqtt.connect(`ws://${host}:${wsPort}`, {
    username: user,
    password: pass,
    connectTimeout: MQTT_CONNECT_TIMEOUT_MS,
  })

  try {
    // Wait for connection (or fail fast)
    await new Promise<void>((resolve, reject) => {
      const timer = setTimeout(
        () => reject(new Error(`MQTT connection timeout after ${MQTT_CONNECT_TIMEOUT_MS}ms`)),
        MQTT_CONNECT_TIMEOUT_MS,
      )
      client.on('connect', () => { clearTimeout(timer); resolve() })
      client.on('error', (err: Error) => { clearTimeout(timer); reject(err) })
    })

    // Publish
    await client.publishAsync(topic, payload, { qos: options?.qos ?? 0 })
  } finally {
    // Always disconnect
    try { await client.endAsync(true) } catch (_) { /* best-effort cleanup */ }
  }
}
