// deno run --allow-net index.ts
import { createClient } from 'jsr:@supabase/supabase-js@2'
import { decodeBase64, encodeBase64 } from 'jsr:@std/encoding/base64'
import { verifyBlePayload, tagBlePayload } from '../_shared/vmflow-payload.ts';

const BLE_FRESHNESS_SEC = 60;

function toScaleFactor(p: number, x: number, y: number): number {
  return p / x / Math.pow(10, -y);
}

function fromScaleFactor(p: number, x: number, y: number): number {
  return p * x * Math.pow(10, -y);
}

Deno.serve(async (req) => {

    try {
        const body = await req.json();

        const payload: Uint8Array = decodeBase64(body.payload)

        const supabase = createClient( Deno.env.get("SUPABASE_URL")!, Deno.env.get('SUPABASE_ANON_KEY')!,
            { global: { headers: { Authorization: req.headers.get('Authorization')! } } }
        )

        // 401 (not 500) on a missing/expired JWT so the app refreshes its session;
        // otherwise RLS just hides the device and it looks like "Unknown device".
        const { data: authData, error: authError } = await supabase.auth.getUser();
        if (authError || !authData?.user) {
            return new Response(JSON.stringify({ message: "Unauthorized" }), {
                headers: { 'Content-Type': 'application/json' },
                status: 401
            })
        }

        const { data: embeddedData, error: embeddedError } = await supabase.from("embedded").select("passkey,subdomain,id,machine_id").eq("subdomain", body.subdomain);

        if (!embeddedData?.length) {
            throw new Error("Unknown device");
        }

        if (payload.length !== 19 || payload[0] !== 0x0a) {
            throw new Error("Invalid payload");
        }
        const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);

        if (!(await verifyBlePayload(embeddedData[0].passkey, payload))) {
            throw new Error("Invalid signature");
        }

        // Same freshness window as the firmware (BLE_FRESHNESS_SEC): a captured
        // vend request cannot be turned into a new approval later.
        const ts = view.getUint32(7);
        if (Math.abs(Math.floor(Date.now() / 1000) - ts) > BLE_FRESHNESS_SEC) {
            throw new Error("Expired payload");
        }

        const itemPrice = view.getUint32(1);
        const itemNumber = view.getUint16(5);

        const { data: saleData, error: salesError } = await supabase.from("sales").insert([{
            embedded_id: embeddedData[0].id,
            machine_id:  embeddedData[0].machine_id ?? null,
            item_number: itemNumber,
            item_price:  fromScaleFactor(itemPrice, 1, 2),
            channel:     "ble",
            lat:         (body.lat !== undefined ? body.lat : null),
            lng:         (body.lng !== undefined ? body.lng : null) }]).select("id, seq").single()

        if (salesError || !saleData) {
            throw new Error(salesError?.message ?? "Could not record sale");
        }

        // Approval: same price/item/ts, cmd APPROVE, bytes 11-14 = sales.seq. The
        // firmware accepts it once (seq above the last accepted) and only for the
        // vend request it is waiting on (price and item must match). Older firmware
        // ignores bytes 11-14 (still covered by the tag).
        payload[0] = 0x03;
        view.setUint32(11, saleData.seq);
        await tagBlePayload(embeddedData[0].passkey, payload);

        return new Response(JSON.stringify({payload: encodeBase64(payload), sales_id: saleData.id}), { headers: { 'Content-Type': 'application/json' } })

    } catch (err) {
        return new Response(JSON.stringify({ message: err?.message ?? err }), {
            headers: { 'Content-Type': 'application/json' },
            status: 500
        })
    }
})
