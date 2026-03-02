import { createClient } from 'jsr:@supabase/supabase-js@2'
import { decodeBase64 } from 'jsr:@std/encoding/base64'
import { sendPushToUsers } from '../_shared/web-push.ts'

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
    // Formats: "online|v:1.0.0|b:Mar  1 2026 14:30:00 +0100", "offline", "ota_updating", "ota_success", "ota_failed"
    if (eventType === 'status') {
      const statusBytes = decodeBase64(payloadB64);
      const rawStatus = new TextDecoder().decode(statusBytes);

      // Parse "online|v:1.0.0|b:Mar  1 2026 14:30:00 +0100"
      // → status = "online", firmware_version = "1.0.0", build_date = ISO timestamp
      const parts = rawStatus.split('|');
      const status = parts[0];
      let firmwareVersion: string | undefined;
      let firmwareBuildDate: string | undefined;

      for (let i = 1; i < parts.length; i++) {
        if (parts[i].startsWith('v:')) {
          firmwareVersion = parts[i].substring(2);
        } else if (parts[i].startsWith('b:')) {
          // C __DATE__ __TIME__ + CMake %z offset: "Mar  1 2026 14:30:00 +0100"
          const raw = parts[i].substring(2);
          const parsed = new Date(raw);
          if (!isNaN(parsed.getTime())) {
            firmwareBuildDate = parsed.toISOString();
          }
        }
      }

      const updatePayload: Record<string, any> = {
        status,
        status_at: new Date().toISOString(),
      };

      // Only update firmware fields when present (don't clear on offline/ota states)
      if (firmwareVersion) {
        updatePayload.firmware_version = firmwareVersion;
      }
      if (firmwareBuildDate) {
        updatePayload.firmware_build_date = firmwareBuildDate;
      }

      const { error } = await adminClient
        .from('embeddeds')
        .update(updatePayload)
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
      .select('passkey, id, owner_id, company')
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

    // Extract timestamp (bytes 8-11, big-endian) for use as sale timestamp
    const timestampSec =
      (payload[8] << 24) |
      (payload[9] << 16) |
      (payload[10] << 8) |
      payload[11];
    const timestampUnsigned = timestampSec >>> 0;

    // No timestamp window validation here — this webhook is already authenticated
    // via X-Webhook-Secret, so replay protection is redundant. Skipping the check
    // allows the MQTT broker to queue messages during forwarder downtime.

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

      const salePrice = fromScaleFactor(itemPrice >>> 0, 1, 2);

      // Use the device's timestamp so queued messages get the correct sale time
      const saleTime = new Date(timestampUnsigned * 1000).toISOString();

      const { error: insertError } = await adminClient
        .from('sales')
        .insert([{
          owner_id: embedded.owner_id,
          embedded_id: embedded.id,
          item_number: itemNumber,
          item_price: salePrice,
          channel,
          created_at: saleTime,
        }]);

      if (insertError) throw insertError;

      // ── Push notification dispatch (best-effort, never blocks sale recording) ──
      try {
        // Look up machine + tray + product once (used by both sale and low-stock notifications)
        const { data: machine } = await adminClient
          .from('vendingMachine')
          .select('id, name')
          .eq('embedded', embedded.id)
          .maybeSingle();

        let productName: string | undefined;
        let productImageUrl: string | undefined;
        let lowTray: { current_stock: number; min_stock: number } | undefined;

        if (machine) {
          const { data: tray } = await adminClient
            .from('machine_trays')
            .select('product_id, current_stock, min_stock')
            .eq('machine_id', machine.id)
            .eq('item_number', itemNumber)
            .maybeSingle();

          if (tray?.product_id) {
            const { data: product } = await adminClient
              .from('products')
              .select('name, image_path')
              .eq('id', tray.product_id)
              .maybeSingle();

            if (product?.name) productName = product.name;
            if (product?.image_path) {
              const supabaseUrl = Deno.env.get('SUPABASE_URL') ?? Deno.env.get('SUPABASE_PUBLIC_URL');
              productImageUrl = `${supabaseUrl}/storage/v1/object/public/product-images/${product.image_path}`;
            }
          }

          if (tray && tray.min_stock > 0 && tray.current_stock <= tray.min_stock) {
            lowTray = { current_stock: tray.current_stock, min_stock: tray.min_stock };
          }
        }

        // 1. Sale notification
        const itemLabel = productName ?? `Item #${itemNumber}`;
        const machineLabel = machine?.name ? ` · ${machine.name}` : '';
        const saleBody = `${itemLabel} — €${salePrice.toFixed(2)} (${channel})${machineLabel}`;

        await sendPushToUsers(adminClient, embedded.company, 'sale', {
          title: 'New Sale',
          body: saleBody,
          icon: productImageUrl,
          data: { type: 'sale', embedded_id: embedded.id },
        });

        // 2. Low stock notification (if applicable)
        if (machine && lowTray) {
          await sendPushToUsers(adminClient, embedded.company, 'low_stock', {
            title: 'Low Stock Alert',
            body: `${productName ?? `Item #${itemNumber}`} in ${machine.name}: ${lowTray.current_stock}/${lowTray.min_stock} remaining`,
            icon: productImageUrl,
            data: { type: 'low_stock', machine_id: machine.id },
          });
        }
      } catch (pushErr) {
        console.error('Push notification error:', pushErr);
      }

      // ── Activity log (best-effort) ──────────────────────────────────────────
      try {
        await adminClient.from('activity_log').insert({
          company_id: embedded.company,
          entity_type: 'sale',
          entity_id: embedded.id,
          action: 'sale_recorded',
          metadata: {
            item_number: itemNumber,
            price: salePrice,
            channel,
            device_id: embedded.id,
          },
        });
      } catch (logErr) {
        console.error('Activity log error:', logErr);
      }
    }

    if (eventType === 'paxcounter') {
      const count = (payload[12] << 8) | payload[13];

      const paxTime = new Date(timestampUnsigned * 1000).toISOString();

      const { error: insertError } = await adminClient
        .from('paxcounter')
        .insert([{
          embedded_id: embedded.id,
          count,
          created_at: paxTime,
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
