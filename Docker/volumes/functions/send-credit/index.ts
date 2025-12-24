// deno run --allow-net index.ts
import { Client } from 'https://deno.land/x/mqtt/deno/mod.ts';
import { createClient } from 'jsr:@supabase/supabase-js@2'

const SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyAgCiAgICAicm9sZSI6ICJhbm9uIiwKICAgICJpc3MiOiAic3VwYWJhc2UtZGVtbyIsCiAgICAiaWF0IjogMTY0MTc2OTIwMCwKICAgICJleHAiOiAxNzk5NTM1NjAwCn0.dc_X5iR_VP_qT0zsiyj_I_OZ2T9FtRU2BBNWN8Bu4GE";

function toScaleFactor(p: number, x: number, y: number): number {
  return p / x / Math.pow(10, -y);
}

function fromScaleFactor(p: number, x: number, y: number): number {
  return p * x * Math.pow(10, -y);
}

Deno.serve(async (req) => {

    try {
        const body = await req.json();

        const supabase = createClient( "https://supabase.vmflow.xyz", SUPABASE_ANON_KEY,
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

        const client = new Client({ url: 'mqtt://mqtt.vmflow.xyz' });
        await client.connect();

        await client.publish(`${embeddedData[0].subdomain}.vmflow.xyz/credit`, payload);
        await client.disconnect();

        if ("online" === embeddedData[0].status){
            await supabase.from("sales").insert([{ embedded_id: embeddedData[0].id, item_price: fromScaleFactor(itemPrice, 1, 2), channel: "mqtt" }])
        }

        return new Response(JSON.stringify({status:'ok'}), { headers: { 'Content-Type': 'application/json' } })

    } catch (err) {
        return new Response(JSON.stringify({ message: err?.message ?? err }), {
            headers: { 'Content-Type': 'application/json' },
            status: 500
        })
    }
})

