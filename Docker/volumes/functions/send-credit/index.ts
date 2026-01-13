// deno run --allow-net index.ts
import { Client } from 'https://deno.land/x/mqtt/deno/mod.ts';
import { createClient } from 'jsr:@supabase/supabase-js@2'

function toScaleFactor(p: number, x: number, y: number): number {
  return p / x / Math.pow(10, -y);
}

function fromScaleFactor(p: number, x: number, y: number): number {
  return p * x * Math.pow(10, -y);
}

Deno.serve(async (req) => {

    try {
        const body = await req.json();

        const supabase = createClient( process.env.SUPABASE_URL, process.env.SUPABASE_ANON_KEY,
            { global: { headers: { Authorization: req.headers.get('Authorization')! } } }
        )

        const { data: embeddedData, error } = await supabase.from("embeddeds").select("passkey,subdomain,status,id").eq("subdomain", body.subdomain);

        const cipher: number[] = [...embeddedData[0].passkey].map(c => c.charCodeAt(0));

        const payload: Uint8Array = new Uint8Array(19)
        crypto.getRandomValues(payload);

        const itemPrice = toScaleFactor(body.amount, 1, 2)

        const timestampSec = Math.floor(new Date().getTime()/1000);

        payload[0] = 0x20;
        payload[1] = 0x01; 			            // version v1
        payload[2] = (itemPrice >> 8) & 0xff;   // itemPrice
        payload[3] = (itemPrice >> 0) & 0xff;
        payload[4] = 0x00; 			            // itemNumber
        payload[5] = 0x00;
        payload[6] = (timestampSec >> 24) & 0xff;
        payload[7] = (timestampSec >> 16) & 0xff;
        payload[8] = (timestampSec >> 8) & 0xff;
        payload[9] = (timestampSec >> 0) & 0xff;

        let chk = payload.slice(0, -1).reduce((acc, val) => acc + val, 0);
        payload[payload.length - 1] = chk;

        for(let k= 0; k < cipher.length; k++){
            payload[k + 1] ^= cipher[k];
        }

        const client = new Client({ url: `mqtt://${process.env.MQTT_HOST}` });
        await client.connect();

        await client.publish(`${embeddedData[0].subdomain}.vmflow.xyz/credit`, payload);
        await client.disconnect();

        let salesId: string | null = null;
        if ("online" === embeddedData[0].status){

            const { data: saleData, error } = await supabase.from("sales").insert([{ embedded_id: embeddedData[0].id, item_price: fromScaleFactor(itemPrice, 1, 2), channel: "mqtt" }]).select("id").single()

            salesId = saleData.id
        }

        return new Response(JSON.stringify({status: embeddedData[0].status, sales_id: salesId}), { headers: { 'Content-Type': 'application/json' } })

    } catch (err) {
        return new Response(JSON.stringify({ message: err?.message ?? err }), {
            headers: { 'Content-Type': 'application/json' },
            status: 500
        })
    }
})

