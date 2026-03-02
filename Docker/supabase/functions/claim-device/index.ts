import { createClient } from 'jsr:@supabase/supabase-js@2'

// Generates a random 18-character passkey (printable ASCII 33-126, excluding ' " \ for NVS safety)
function generatePasskey(): string {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+[]{}|;:,.<>?'
  const bytes = new Uint8Array(18)
  crypto.getRandomValues(bytes)
  return Array.from(bytes).map(b => chars[b % chars.length]).join('')
}

Deno.serve(async (req) => {
  // Only accept POST
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), {
      status: 405, headers: { 'Content-Type': 'application/json' },
    })
  }

  try {
    const body = await req.json()
    const { short_code, mac_address } = body

    if (!short_code || typeof short_code !== 'string') {
      return new Response(JSON.stringify({ error: 'short_code is required' }), {
        status: 400, headers: { 'Content-Type': 'application/json' },
      })
    }

    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    // Look up the provisioning token
    const { data: token, error: tokenError } = await adminClient
      .from('device_provisioning')
      .select('id, company_id, created_by, short_code, expires_at, used_at, embedded_id, name, device_only')
      .eq('short_code', short_code.toUpperCase())
      .maybeSingle()

    if (tokenError) throw tokenError

    if (!token) {
      return new Response(JSON.stringify({ error: 'Invalid provisioning code' }), {
        status: 404, headers: { 'Content-Type': 'application/json' },
      })
    }

    // Idempotent retry: token already used but device exists — return existing credentials
    if (token.used_at && token.embedded_id) {
      const { data: existing, error: existingError } = await adminClient
        .from('embeddeds')
        .select('id, passkey')
        .eq('id', token.embedded_id)
        .single()

      if (existingError) throw existingError

      return new Response(
        JSON.stringify({
          company_id: token.company_id,
          device_id: existing.id,
          passkey: existing.passkey,
          mqtt_host: Deno.env.get('MQTT_PUBLIC_HOST') ?? 'mqtt.vmflow.xyz',
          mqtt_port: Deno.env.get('MQTT_PUBLIC_PORT') ?? '1883',
          mqtt_user: 'vmflow',
          mqtt_pass: 'vmflow',
        }),
        { status: 200, headers: { 'Content-Type': 'application/json' } }
      )
    }

    if (token.used_at) {
      return new Response(JSON.stringify({ error: 'Provisioning code already used' }), {
        status: 409, headers: { 'Content-Type': 'application/json' },
      })
    }
    if (new Date(token.expires_at) < new Date()) {
      return new Response(JSON.stringify({ error: 'Provisioning code expired' }), {
        status: 410, headers: { 'Content-Type': 'application/json' },
      })
    }

    const passkey = generatePasskey()

    // Create embeddeds row — subdomain auto-increments
    const { data: embedded, error: embeddedError } = await adminClient
      .from('embeddeds')
      .insert({
        company: token.company_id,
        owner_id: token.created_by,
        mac_address: mac_address ?? null,
        passkey,
        status: 'offline',
      })
      .select('id, subdomain')
      .single()

    if (embeddedError) throw embeddedError

    // Create vendingMachine row linked to the new embedded device (unless device_only)
    if (!token.device_only) {
      const { error: machineError } = await adminClient
        .from('vendingMachine')
        .insert({
          company: token.company_id,
          embedded: embedded.id,
          name: token.name ?? (mac_address ? `Device ${mac_address}` : `Device ${embedded.subdomain}`),
        })

      if (machineError) throw machineError
    }

    // Mark token as used
    const { error: updateError } = await adminClient
      .from('device_provisioning')
      .update({ used_at: new Date().toISOString(), embedded_id: embedded.id })
      .eq('id', token.id)

    if (updateError) throw updateError

    return new Response(
      JSON.stringify({
        company_id: token.company_id,
        device_id: embedded.id,
        passkey,
        mqtt_host: Deno.env.get('MQTT_PUBLIC_HOST') ?? 'mqtt.vmflow.xyz',
        mqtt_port: Deno.env.get('MQTT_PUBLIC_PORT') ?? '1883',
        mqtt_user: 'vmflow',
        mqtt_pass: 'vmflow',
      }),
      { status: 200, headers: { 'Content-Type': 'application/json' } }
    )
  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500, headers: { 'Content-Type': 'application/json' },
    })
  }
})
