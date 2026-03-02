import mqtt from "npm:mqtt@5";
import { encodeBase64 } from "jsr:@std/encoding/base64";

const MQTT_HOST = Deno.env.get("MQTT_HOST") ?? "broker";
const MQTT_USER = Deno.env.get("MQTT_FORWARDER_USER") ?? "forwarder";
const MQTT_PASS = Deno.env.get("MQTT_FORWARDER_PASS") ?? "";
const SUPABASE_URL = Deno.env.get("SUPABASE_URL") ?? "";
const WEBHOOK_SECRET = Deno.env.get("MQTT_WEBHOOK_SECRET") ?? "";

// Topics: /{company_id}/{device_id}/{event}
// Leading / creates empty first level, so pattern is /+/+/{event}
const topics = ["/+/+/sale", "/+/+/status", "/+/+/paxcounter"];

const client = mqtt.connect(`mqtt://${MQTT_HOST}:1883`, {
  clientId: "vmflow-forwarder",
  clean: false, // persistent session — broker queues QoS 1 messages while we're offline
  reconnectPeriod: 5000,
  username: MQTT_USER,
  password: MQTT_PASS,
});

client.on("connect", (connack: { sessionPresent: boolean }) => {
  console.log(`Connected to mqtt://${MQTT_HOST}:1883 (session present: ${connack.sessionPresent})`);
  if (!connack.sessionPresent) {
    // First connect or broker lost our session — need to subscribe
    client.subscribe(topics, { qos: 1 }, (err) => {
      if (err) console.error("Subscribe error:", err);
      else console.log("Subscribed to:", topics.join(", "));
    });
  } else {
    console.log("Resuming existing session, subscriptions already active");
  }
});

client.on("message", async (topic: string, payload: Buffer) => {
  try {
    const res = await fetch(`${SUPABASE_URL}/functions/v1/mqtt-webhook`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-Webhook-Secret": WEBHOOK_SECRET,
      },
      body: JSON.stringify({
        topic,
        payload: encodeBase64(new Uint8Array(payload)),
      }),
    });
    console.log(`${topic} -> ${res.status}`);
  } catch (err) {
    console.error(`Error forwarding ${topic}:`, err);
  }
});

client.on("error", (err: Error) => console.error("MQTT error:", err));
client.on("reconnect", () => console.log("Reconnecting to MQTT..."));
client.on("close", () => console.log("MQTT connection closed"));
