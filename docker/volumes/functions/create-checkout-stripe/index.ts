import { createClient } from 'jsr:@supabase/supabase-js@2'
import Stripe from 'https://esm.sh/stripe@14.25.0?target=deno'
import { DEFAULT_CURRENCY, toStripeAmount } from '../_shared/currency.ts'

Deno.serve(async (req) => {
  const corsHeaders = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
  }

  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const { machineId, amount } = await req.json()

    if (!machineId || !amount) {
      throw new Error('Machine ID and amount are required')
    }

    const supabase = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // 1. Get machine and its owner
    const { data: machine, error: machineError } = await supabase
      .from('machines')
      .select('name, owner_id')
      .eq('id', machineId)
      .single()

    if (machineError || !machine) throw new Error('Machine not found')

    // 2. Get operator's Stripe Secret Key
    const { data: credential, error: credError } = await supabase
      .from('credentials')
      .select('value')
      .eq('owner_id', machine.owner_id)
      .eq('key', 'stripe_secret_key')
      .maybeSingle()

    if (credError || !credential) {
      throw new Error('Stripe is not configured for this operator.')
    }

    const stripe = new Stripe(credential.value, {
      apiVersion: '2023-10-16',
      httpClient: Stripe.createFetchHttpClient(),
    })

    const { data: currencyCred } = await supabase
      .from('credentials')
      .select('value')
      .eq('owner_id', machine.owner_id)
      .eq('key', 'currency')
      .maybeSingle()

    const currency = (currencyCred?.value ?? DEFAULT_CURRENCY).toLowerCase()

    // 3. Create Checkout Session
    const session = await stripe.checkout.sessions.create({
      line_items: [
        {
          price_data: {
            currency,
            product_data: {
              name: `Credit for ${machine.name}`,
              metadata: { machineId },
            },
            unit_amount: toStripeAmount(amount, currency),
          },
          quantity: 1,
        },
      ],
      mode: 'payment',
      success_url: `${req.headers.get('origin')}/pay/stripe/${machineId}?success=true`,
      cancel_url: `${req.headers.get('origin')}/pay/stripe/${machineId}?canceled=true`,
      metadata: {
        machineId,
        operatorId: machine.owner_id,
      },
    })

    return new Response(JSON.stringify({ url: session.url }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200,
    })
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400,
    })
  }
})
