
// HMAC-SHA256(passkey, msg) as lowercase hex. Used to sign MQTT RPC envelopes
// "<cmd>[:<args>]:<ts>:<hmac>" verified by the device firmware.
export async function hmacHex(passkey: string, msg: string): Promise<string> {
  const key = await crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(passkey),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const sig = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(msg));
  return [...new Uint8Array(sig)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Build the signed RPC credit line for publishing to "<sub>.vmflow.xyz/rpc".
// amountCents = credit amount scaled to 1/100 units (integer).
export async function signCreditRpc(passkey: string, amountCents: number): Promise<string> {
  const ts = Math.floor(Date.now() / 1000);
  const msg = `credit:${amountCents}:${ts}`;
  return `${msg}:${await hmacHex(passkey, msg)}`;
}

export function encodePayloadWithXOR(passkey: string, payload: Uint8Array): Uint8Array {

  let chk = payload.slice(0, -1).reduce((acc, val) => acc + val, 0);
  payload[payload.length - 1] = chk;

  for(let k= 0; k < passkey.length; k++){
    payload[k + 1] ^= passkey.charCodeAt(k);
  }

  return payload
}

export function decodePayloadWithXOR(passkey: string, payload: Uint8Array): Uint8Array {

  for(let k= 0; k < passkey.length; k++){
    payload[k + 1] ^= passkey.charCodeAt(k);
  }

  let chk = payload.slice(0, -1).reduce((acc, val) => acc + val, 0);

  if(payload[payload.length - 1] !== (chk & 0xff)){
    throw new Error("Invalid checksum");
  }

  return payload
}
