import { createClient } from 'jsr:@supabase/supabase-js@2'
import { decodeBase64 } from 'jsr:@std/encoding/base64'

function fromScaleFactor(p: number, x: number, y: number): number {
  return p * x * Math.pow(10, -y);
}

Deno.serve(async (req) => {
  try {
    // Verify webhook secret
    const secret = req.headers.get('X-Webhook-Secret');
    const expectedSecret = Deno.env.get('MQTT_WEBHOOK_SECRET');
    if (!expectedSecret || secret !== expectedSecret) {
      return new Response(JSON.stringify({ error: 'unauthorized' }), { status: 401 });
    }

    const body = await req.json();
    const { topic, payload: payloadB64 } = body;

    // Parse topic: /{company_id}/{device_id}/{event_type}
    const match = topic.match(/^\/([^/]+)\/([^/]+)\/(sale|status|paxcounter)$/);
    if (!match) {
      return new Response(JSON.stringify({ error: 'invalid topic' }), { status: 400 });
    }

    const companyId = match[1];
    const deviceId = match[2];
    const eventType = match[3];

    // Service-role admin client (machine-to-machine, no user auth)
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    );

    // Status event: simple UTF-8 payload, no encryption
    if (eventType === 'status') {
      const statusBytes = decodeBase64(payloadB64);
      const status = new TextDecoder().decode(statusBytes);

      const { error } = await adminClient
        .from('embeddeds')
        .update({ status, status_at: new Date().toISOString() })
        .eq('id', deviceId);

      if (error) throw error;
      return new Response(JSON.stringify({ ok: true }), { status: 200 });
    }

    // Sale and paxcounter: encrypted payload
    const payload = new Uint8Array(decodeBase64(payloadB64));

    if (payload.length !== 19) {
      return new Response(JSON.stringify({ error: 'invalid payload length' }), { status: 400 });
    }

    // Look up device
    const { data: embeddedData, error: lookupError } = await adminClient
      .from('embeddeds')
      .select('passkey, id, owner_id')
      .eq('id', deviceId);

    if (lookupError) throw lookupError;
    if (!embeddedData || embeddedData.length === 0) {
      return new Response(JSON.stringify({ error: 'device not found' }), { status: 404 });
    }

    const embedded = embeddedData[0];
    const passkey: number[] = [...embedded.passkey].map((c: string) => c.charCodeAt(0));

    // XOR decrypt bytes 1-18 with passkey
    for (let k = 0; k < passkey.length; k++) {
      payload[k + 1] ^= passkey[k];
    }

    // Validate checksum: sum(bytes 0..17) & 0xFF === byte 18
    const chk = payload.slice(0, -1).reduce((acc, val) => acc + val, 0);
    if (payload[payload.length - 1] !== (chk & 0xff)) {
      return new Response(JSON.stringify({ error: 'checksum mismatch' }), { status: 400 });
    }

    // Validate timestamp (bytes 8-11, big-endian) within ±8s
    const timestampSec =
      (payload[8] << 24) |
      (payload[9] << 16) |
      (payload[10] << 8) |
      payload[11];
    // Convert to unsigned 32-bit
    const timestampUnsigned = timestampSec >>> 0;
    const currentTime = Math.floor(Date.now() / 1000);

    if (Math.abs(currentTime - timestampUnsigned) > 8) {
      return new Response(JSON.stringify({ error: 'timestamp out of range' }), { status: 400 });
    }

    if (eventType === 'sale') {
      const cmd = payload[0];
      const itemPrice =
        (payload[2] << 24) |
        (payload[3] << 16) |
        (payload[4] << 8) |
        payload[5];
      const itemNumber = (payload[6] << 8) | payload[7];

      // 0x21 = CASH_SALE (coin/bill), 0x23 = CARD_SALE (credit card / cashless device #2)
      const channel = cmd === 0x23 ? 'card' : 'cash';

      const { error: insertError } = await adminClient
        .from('sales')
        .insert([{
          owner_id: embedded.owner_id,
          embedded_id: embedded.id,
          item_number: itemNumber,
          item_price: fromScaleFactor(itemPrice >>> 0, 1, 2),
          channel,
        }]);

      if (insertError) throw insertError;
    }

    if (eventType === 'paxcounter') {
      const count = (payload[12] << 8) | payload[13];

      const { error: insertError } = await adminClient
        .from('paxcounter')
        .insert([{
          owner_id: embedded.owner_id,
          embedded_id: embedded.id,
          count,
        }]);

      if (insertError) throw insertError;
    }

    return new Response(JSON.stringify({ ok: true }), { status: 200 });

  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500,
      headers: { 'Content-Type': 'application/json' },
    });
  }
});
