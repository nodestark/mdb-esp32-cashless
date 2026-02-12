// deno run --allow-net index.ts
import { createClient } from 'jsr:@supabase/supabase-js@2'
import { decodeBase64, encodeBase64 } from 'jsr:@std/encoding/base64'

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

        const { data: embeddedData, error } = await supabase.from("embeddeds").select("passkey,subdomain,id").eq("subdomain", body.subdomain);

        const passkey: number[] = [...embeddedData[0].passkey].map(c => c.charCodeAt(0));

        for(let k= 0; k < passkey.length; k++){
            payload[k + 1] ^= passkey[k];
        }
        let chk = payload.slice(0, -1).reduce((acc, val) => acc + val, 0);

        if( payload[payload.length - 1] == (chk & 0xff) ){

            const itemPrice =
                (payload[2] << 24) |
                (payload[3] << 16) |
                (payload[4] << 8) |
                (payload[5] << 0);
            const itemNumber = (payload[6] << 8) | (payload[7] << 0);

	        const { data: saleData, error } = await supabase.from("sales").insert([{
            	embedded_id: embeddedData[0].id,
            	item_number: itemNumber,
            	item_price: fromScaleFactor(itemPrice, 1, 2),
            	channel: "ble",
            	lat: (body.lat !== undefined ? body.lat : null),
		        lng: (body.lng !== undefined ? body.lng : null) }]).select("id").single()

            payload[0] = 0x03;

            let chk_ = payload.slice(0, -1).reduce((acc, val) => acc + val, 0);
            payload[payload.length - 1] = chk_;

            for(let k= 0; k < passkey.length; k++){
                payload[k + 1] ^= passkey[k];
            }

            return new Response(JSON.stringify({payload: encodeBase64(payload), sales_id: saleData.id}), { headers: { 'Content-Type': 'application/json' } })

        }
        return new Response(JSON.stringify({ }), { headers: { 'Content-Type': 'application/json' } })

    } catch (err) {
        return new Response(JSON.stringify({ message: err?.message ?? err }), {
            headers: { 'Content-Type': 'application/json' },
            status: 500
        })
    }
})
