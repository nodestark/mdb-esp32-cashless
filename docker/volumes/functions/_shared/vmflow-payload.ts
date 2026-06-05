
// HMAC-SHA256(passkey, data) raw bytes (32). Shared by the hex envelope signing
// (MQTT) and the truncated-tag BLE payload helpers below.
async function hmacRaw(passkey: string, data: BufferSource): Promise<Uint8Array> {
  const key = await crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(passkey),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  return new Uint8Array(await crypto.subtle.sign("HMAC", key, data));
}

// HMAC-SHA256(passkey, msg) as lowercase hex. Used to sign MQTT RPC envelopes
// "<cmd>[:<args>]:<ts>:<hmac>" verified by the device firmware.
export async function hmacHex(passkey: string, msg: string): Promise<string> {
  const sig = await hmacRaw(passkey, new TextEncoder().encode(msg));
  return [...sig].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Build the signed RPC credit line for publishing to "<sub>.vmflow.xyz/rpc".
// amountCents = credit amount scaled to 1/100 units (integer).
export async function signCreditRpc(passkey: string, amountCents: number): Promise<string> {
  const ts = Math.floor(Date.now() / 1000);
  const msg = `credit:${amountCents}:${ts}`;
  return `${msg}:${await hmacHex(passkey, msg)}`;
}

// BLE wire payload — 19 bytes: plaintext fields in bytes 0..14, authenticated by
// a 4-byte truncated HMAC tag in bytes 15..18 (= HMAC(passkey, bytes 0..14)[:4]).
// Mirrors ble_encode_with_passkey / ble_decode_with_passkey in the firmware.

// Verify the tag of a received 19-byte BLE payload (constant-time compare).
export async function verifyBlePayload(passkey: string, payload: Uint8Array): Promise<boolean> {
  const tag = (await hmacRaw(passkey, payload.slice(0, 15))).slice(0, 4);
  let diff = 0;
  for (let i = 0; i < 4; i++) diff |= tag[i] ^ payload[15 + i];
  return diff === 0;
}

// Stamp the 4-byte truncated HMAC tag into bytes 15..18 (call after setting fields).
export async function tagBlePayload(passkey: string, payload: Uint8Array): Promise<void> {
  const tag = (await hmacRaw(passkey, payload.slice(0, 15))).slice(0, 4);
  payload.set(tag, 15);
}
