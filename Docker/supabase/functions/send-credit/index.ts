import { Client } from 'https://deno.land/x/mqtt/deno/mod.ts';
import { createClient } from 'jsr:@supabase/supabase-js@2'

function toScaleFactor(p: number, x: number, y: number): number {
  return p / x / Math.pow(10, -y);
}

function fromScaleFactor(p: number, x: number, y: number): number {
  return p * x * Math.pow(10, -y);
}

async function hashKey(key: string): Promise<string> {
  const encoded = new TextEncoder().encode(key)
  const hash = await crypto.subtle.digest('SHA-256', encoded)
  return Array.from(new Uint8Array(hash)).map(b => b.toString(16).padStart(2, '0')).join('')
}

Deno.serve(async (req) => {
  try {
    const body = await req.json();

    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    const apiKey = req.headers.get('X-API-Key')
    const authHeader = req.headers.get('Authorization')
    let companyId: string | null = null

    if (apiKey) {
      // --- API key authentication ---
      const keyHash = await hashKey(apiKey)
      const { data: keyData, error: keyError } = await adminClient
        .from('api_keys')
        .select('id, company_id, revoked_at')
        .eq('key_hash', keyHash)
        .maybeSingle()

      if (keyError || !keyData) {
        return new Response(JSON.stringify({ error: 'Invalid API key' }), {
          status: 401, headers: { 'Content-Type': 'application/json' },
        })
      }
      if (keyData.revoked_at) {
        return new Response(JSON.stringify({ error: 'API key has been revoked' }), {
          status: 401, headers: { 'Content-Type': 'application/json' },
        })
      }

      companyId = keyData.company_id

      // Update last_used_at (fire-and-forget)
      adminClient
        .from('api_keys')
        .update({ last_used_at: new Date().toISOString() })
        .eq('id', keyData.id)
        .then()
    } else if (authHeader) {
      // --- JWT authentication ---
      const token = authHeader.replace('Bearer ', '')
      const { data: { user }, error: userError } = await adminClient.auth.getUser(token)
      if (userError || !user) {
        return new Response(JSON.stringify({ error: 'Unauthorized' }), {
          status: 401, headers: { 'Content-Type': 'application/json' },
        })
      }

      const { data: membership } = await adminClient
        .from('organization_members')
        .select('company_id, role')
        .eq('user_id', user.id)
        .maybeSingle()

      if (!membership || membership.role !== 'admin') {
        return new Response(JSON.stringify({ error: 'Admin role required' }), {
          status: 403, headers: { 'Content-Type': 'application/json' },
        })
      }

      companyId = membership.company_id
    } else {
      return new Response(JSON.stringify({ error: 'Authorization required. Use Authorization header or X-API-Key header.' }), {
        status: 401, headers: { 'Content-Type': 'application/json' },
      })
    }

    // Fetch device using service_role (works for both auth methods)
    const { data: embeddedData, error: embeddedError } = await adminClient
      .from('embeddeds')
      .select('passkey, status, id, company')
      .eq('id', body.device_id)
      .eq('company', companyId)
      .maybeSingle()

    if (embeddedError || !embeddedData) {
      return new Response(JSON.stringify({ error: 'Device not found or not in your organization' }), {
        status: 404, headers: { 'Content-Type': 'application/json' },
      })
    }

    const cipher: number[] = [...embeddedData.passkey].map((c: string) => c.charCodeAt(0));

    const payload: Uint8Array = new Uint8Array(19)
    crypto.getRandomValues(payload);

    const itemPrice = toScaleFactor(body.amount, 1, 2)
    const timestampSec = Math.floor(new Date().getTime() / 1000);

    payload[0] = 0x20;
    payload[1] = 0x01;                          // version v1
    payload[2] = (itemPrice >> 24) & 0xff;      // itemPrice
    payload[3] = (itemPrice >> 16) & 0xff;
    payload[4] = (itemPrice >> 8) & 0xff;
    payload[5] = (itemPrice >> 0) & 0xff;
    payload[6] = 0x00;                          // itemNumber
    payload[7] = 0x00;
    payload[8] = (timestampSec >> 24) & 0xff;
    payload[9] = (timestampSec >> 16) & 0xff;
    payload[10] = (timestampSec >> 8) & 0xff;
    payload[11] = (timestampSec >> 0) & 0xff;

    let chk = payload.slice(0, -1).reduce((acc, val) => acc + val, 0);
    payload[payload.length - 1] = chk;

    for (let k = 0; k < cipher.length; k++) {
      payload[k + 1] ^= cipher[k];
    }

    const mqttHost = Deno.env.get('MQTT_HOST') ?? 'mqtt.vmflow.xyz'
    const client = new Client({ url: `mqtt://${mqttHost}` });
    await client.connect();
    await client.publish(`/${embeddedData.company}/${embeddedData.id}/credit`, payload);
    await client.disconnect();

    let salesId: string | null = null;
    if (embeddedData.status === 'online') {
      const { data: saleData, error: saleError } = await adminClient
        .from('sales')
        .insert([{ embedded_id: embeddedData.id, item_price: fromScaleFactor(itemPrice, 1, 2), channel: 'mqtt' }])
        .select('id')
        .single()

      if (!saleError && saleData) {
        salesId = saleData.id
      }
    }

    return new Response(JSON.stringify({ status: embeddedData.status, sales_id: salesId }), {
      headers: { 'Content-Type': 'application/json' },
    })
  } catch (err) {
    return new Response(JSON.stringify({ message: err?.message ?? err }), {
      headers: { 'Content-Type': 'application/json' },
      status: 500,
    })
  }
})
