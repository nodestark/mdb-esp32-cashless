// deno run --allow-net index.ts
import { Client } from 'https://deno.land/x/mqtt/deno/mod.ts';
import { createClient } from 'jsr:@supabase/supabase-js@2'
import { signCreditRpc } from '../_shared/vmflow-payload.ts';

function toScaleFactor(p: number, x: number, y: number): number {
  return p / x / Math.pow(10, -y);
}

function fromScaleFactor(p: number, x: number, y: number): number {
  return p * x * Math.pow(10, -y);
}

Deno.serve(async (req) => {

    try {
        const body = await req.json();

        const supabase = createClient( Deno.env.get("SUPABASE_URL")!, Deno.env.get('SUPABASE_ANON_KEY')!,
            { global: { headers: { Authorization: req.headers.get('Authorization')! } } }
        )

        const { data: embeddedData, error } = await supabase.from("embedded").select("passkey,subdomain,status,id,machine_id").eq("subdomain", body.subdomain);

        const itemPrice = toScaleFactor(body.amount, 1, 2)

        const creditLine = await signCreditRpc(embeddedData[0].passkey, itemPrice);

        const client = new Client({ url: `mqtt://mqtt.vmflow.xyz` });
        await client.connect();

        await client.publish(`${embeddedData[0].subdomain}.vmflow.xyz/rpc`, creditLine);
        await client.disconnect();

        let salesId: string | null = null;
        if ("online" === embeddedData[0].status){

            const { data: saleData, error } = await supabase.from("sales").insert([{ embedded_id: embeddedData[0].id, machine_id: embeddedData[0].machine_id ?? null, item_price: fromScaleFactor(itemPrice, 1, 2), channel: "mqtt" }]).select("id").single()

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