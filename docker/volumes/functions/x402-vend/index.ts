// deno run --allow-net index.ts
//
// x402 payment rail for VMflow — sibling to create-checkout-stripe / create-checkout-mp.
// x402 (https://x402.org) is an HTTP-native payment protocol: a request without
// payment gets a 402 response describing what to pay; the client retries with a
// signed X-PAYMENT header; the server verifies and settles it via a facilitator,
// then fulfills. No webhook needed — settlement is synchronous, so this single
// function replaces a checkout+webhook pair.
//
// Flow: POST { subdomain, amount }
//   1. no X-PAYMENT header  -> 402 + accepts[] (price, operator wallet, network)
//   2. with X-PAYMENT       -> facilitator /verify -> /settle -> signed MQTT
//                              credit to the machine -> sales insert
//
// Operator setup: one row in `credentials` per operator,
//   key = 'x402_pay_to', value = the wallet address that receives payments
// (same pattern as 'stripe_secret_key' / 'mp_access_token').
//
// Env (all optional — the defaults below settle USDC on Base mainnet):
//   X402_FACILITATOR_URL — facilitator base URL (/verify + /settle)
//   X402_NETWORK         — payment network id (default: base)
//   X402_ASSET           — ERC-20 asset address (default: USDC on Base)
//
// The default facilitator must support the configured network. Note that the
// reference facilitator at x402.org/facilitator is testnet-only, so it is not the
// default here; point X402_FACILITATOR_URL at it (with X402_NETWORK=base-sepolia)
// to run this on a testnet.

import { Client } from 'https://deno.land/x/mqtt/deno/mod.ts';
import { createClient } from 'jsr:@supabase/supabase-js@2'
import { signCreditRpc } from '../_shared/vmflow-payload.ts';

const FACILITATOR_URL = Deno.env.get('X402_FACILITATOR_URL') ?? 'https://facilitator.payai.network';
const NETWORK = Deno.env.get('X402_NETWORK') ?? 'base';
const ASSET = Deno.env.get('X402_ASSET') ?? '0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913'; // USDC on Base
const X402_VERSION = 1;

const corsHeaders = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type, x-payment',
};

// USD amount -> atomic USDC units (6 decimals)
function toAtomic(amount: number): string {
    return String(Math.round(amount * 1e6));
}

function paymentRequirements(amount: number, payTo: string, resource: string) {
    return {
        scheme: 'exact',
        network: NETWORK,
        maxAmountRequired: toAtomic(amount),
        resource,
        description: 'VMflow vending machine credit',
        mimeType: 'application/json',
        payTo,
        maxTimeoutSeconds: 60,
        asset: ASSET,
        extra: { name: 'USD Coin', version: '2' },
    };
}

Deno.serve(async (req) => {

    if (req.method === 'OPTIONS') {
        return new Response('ok', { headers: corsHeaders });
    }

    try {
        const body = await req.json();
        if (!body.subdomain || !body.amount) throw new Error('subdomain and amount are required');

        // Payer is not a logged-in user (same as the Stripe webhook) -> service role
        const supabase = createClient(
            Deno.env.get('SUPABASE_URL')!,
            Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
        );

        // 1. Machine + operator
        const { data: embeddedData, error: embeddedError } = await supabase
            .from('embedded')
            .select('passkey, subdomain, status, id, machine_id')
            .eq('subdomain', body.subdomain)
            .single();

        if (embeddedError || !embeddedData) throw new Error('Machine not found');

        const { data: machine } = await supabase
            .from('machines')
            .select('owner_id')
            .eq('id', embeddedData.machine_id)
            .single();

        if (!machine) throw new Error('Machine not bound to an operator');

        // 2. Operator's receiving wallet
        const { data: cred } = await supabase
            .from('credentials')
            .select('value')
            .eq('owner_id', machine.owner_id)
            .eq('key', 'x402_pay_to')
            .maybeSingle();

        if (!cred?.value) throw new Error('x402 not configured for this operator.');

        const requirements = paymentRequirements(body.amount, cred.value, req.url);

        // 3. No payment header -> quote (this is the 402 in x402)
        const paymentHeader = req.headers.get('x-payment');
        if (!paymentHeader) {
            return new Response(JSON.stringify({
                x402Version: X402_VERSION,
                error: 'X-PAYMENT header is required',
                accepts: [requirements],
            }), { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 402 });
        }

        // A malformed header is a payment problem, not a server fault: answer 402 with the
        // quote so the client can retry, never 500.
        let paymentPayload;
        try {
            paymentPayload = JSON.parse(atob(paymentHeader));
        } catch {
            return new Response(JSON.stringify({
                x402Version: X402_VERSION,
                error: 'Malformed X-PAYMENT header',
                accepts: [requirements],
            }), { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 402 });
        }

        // 4. Verify, then settle
        const verifyRes = await fetch(`${FACILITATOR_URL}/verify`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ x402Version: X402_VERSION, paymentPayload, paymentRequirements: requirements }),
        });
        const verify = await verifyRes.json();
        if (!verify.isValid) {
            return new Response(JSON.stringify({
                x402Version: X402_VERSION,
                error: verify.invalidReason ?? 'Payment verification failed',
                accepts: [requirements],
            }), { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 402 });
        }

        const settleRes = await fetch(`${FACILITATOR_URL}/settle`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ x402Version: X402_VERSION, paymentPayload, paymentRequirements: requirements }),
        });
        const settlement = await settleRes.json();
        if (!settlement.success) {
            return new Response(JSON.stringify({
                x402Version: X402_VERSION,
                error: settlement.errorReason ?? 'Payment settlement failed',
                accepts: [requirements],
            }), { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 402 });
        }

        // 5. Grant credit — identical to send-credit / webhook-stripe
        const itemPrice = Math.round(body.amount * 100);
        const creditLine = await signCreditRpc(embeddedData.passkey, itemPrice);

        const client = new Client({ url: `mqtt://mqtt.vmflow.xyz` });
        await client.connect();
        await client.publish(`${embeddedData.subdomain}.vmflow.xyz/rpc`, creditLine);
        await client.disconnect();

        // 6. Record sale
        let salesId: string | null = null;
        if ('online' === embeddedData.status) {
            const { data: saleData } = await supabase.from('sales').insert([{
                embedded_id: embeddedData.id,
                machine_id: embeddedData.machine_id ?? null,
                item_price: body.amount,
                channel: 'mqtt',
                owner_id: machine.owner_id,
            }]).select('id').single();
            salesId = saleData?.id ?? null;
        }

        return new Response(JSON.stringify({ status: embeddedData.status, sales_id: salesId }), {
            headers: {
                ...corsHeaders,
                'Content-Type': 'application/json',
                'X-PAYMENT-RESPONSE': btoa(JSON.stringify({
                    success: true,
                    transaction: settlement.transaction,
                    network: settlement.network,
                    payer: settlement.payer,
                })),
            },
        });

    } catch (err) {
        return new Response(JSON.stringify({ message: err?.message ?? err }), {
            headers: { ...corsHeaders, 'Content-Type': 'application/json' },
            status: 500,
        });
    }
})
