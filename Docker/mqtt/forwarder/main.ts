import mqtt from "npm:mqtt@5";
import { encodeBase64 } from "jsr:@std/encoding/base64";

const MQTT_HOST = Deno.env.get("MQTT_HOST") ?? "broker";
const SUPABASE_URL = Deno.env.get("SUPABASE_URL") ?? "";
const WEBHOOK_SECRET = Deno.env.get("MQTT_WEBHOOK_SECRET") ?? "";

// Topics: /{company_id}/{device_id}/{event}
// Leading / creates empty first level, so pattern is /+/+/{event}
const topics = ["/+/+/sale", "/+/+/status", "/+/+/paxcounter"];

const client = mqtt.connect(`mqtt://${MQTT_HOST}:1883`, {
  reconnectPeriod: 5000,
});

client.on("connect", () => {
  console.log(`Connected to mqtt://${MQTT_HOST}:1883`);
  client.subscribe(topics, (err) => {
    if (err) console.error("Subscribe error:", err);
    else console.log("Subscribed to:", topics.join(", "));
  });
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
