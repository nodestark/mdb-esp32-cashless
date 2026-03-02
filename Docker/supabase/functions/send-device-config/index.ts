import { Client } from 'https://deno.land/x/mqtt/deno/mod.ts'
import { createClient } from 'jsr:@supabase/supabase-js@2'

Deno.serve(async (req) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), {
      status: 405, headers: { 'Content-Type': 'application/json' },
    })
  }

  try {
    const body = await req.json()
    const { device_id, config } = body

    if (!device_id || !config || typeof config !== 'object') {
      return new Response(JSON.stringify({ error: 'device_id and config are required' }), {
        status: 400, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Validate config values ──────────────────────────────────────────────
    const dbUpdate: Record<string, unknown> = {}
    const mqttPayload: Record<string, unknown> = {}

    if (config.mdb_address !== undefined) {
      if (config.mdb_address !== 1 && config.mdb_address !== 2) {
        return new Response(JSON.stringify({ error: 'mdb_address must be 1 or 2' }), {
          status: 400, headers: { 'Content-Type': 'application/json' },
        })
      }
      dbUpdate.mdb_address = config.mdb_address
      mqttPayload.mdb_address = config.mdb_address
    }

    if (Object.keys(mqttPayload).length === 0) {
      return new Response(JSON.stringify({ error: 'No valid config keys provided' }), {
        status: 400, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Authenticate caller via JWT ─────────────────────────────────────────
    const supabase = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_ANON_KEY')!,
      { global: { headers: { Authorization: req.headers.get('Authorization')! } } }
    )

    const { data: { user }, error: authError } = await supabase.auth.getUser()
    if (authError || !user) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), {
        status: 401, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Verify admin role ───────────────────────────────────────────────────
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!,
    )

    const { data: membership } = await adminClient
      .from('organization_members')
      .select('role')
      .eq('user_id', user.id)
      .single()

    if (!membership || membership.role !== 'admin') {
      return new Response(JSON.stringify({ error: 'Admin access required' }), {
        status: 403, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Look up device (user-scoped for RLS) ────────────────────────────────
    const { data: device, error: deviceError } = await supabase
      .from('embeddeds')
      .select('id, company, status')
      .eq('id', device_id)
      .single()

    if (deviceError || !device) {
      return new Response(JSON.stringify({ error: 'Device not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Update DB ───────────────────────────────────────────────────────────
    const { error: updateError } = await adminClient
      .from('embeddeds')
      .update(dbUpdate)
      .eq('id', device.id)

    if (updateError) throw updateError

    // ── Publish to MQTT ─────────────────────────────────────────────────────
    const mqttHost = Deno.env.get('MQTT_HOST') || 'mqtt.vmflow.xyz'
    const mqttUser = Deno.env.get('MQTT_ADMIN_USER') || 'admin'
    const mqttPass = Deno.env.get('MQTT_ADMIN_PASS') || 'admin'
    const client = new Client({ url: `mqtt://${mqttUser}:${mqttPass}@${mqttHost}` })
    await client.connect()

    const topic = `/${device.company}/${device.id}/config`
    await client.publish(topic, new TextEncoder().encode(JSON.stringify(mqttPayload)), { qos: 1 })
    await client.disconnect()

    // ── Activity log (best-effort) ──────────────────────────────────────────
    try {
      await adminClient.from('activity_log').insert({
        company_id: device.company,
        user_id: user.id,
        entity_type: 'device',
        entity_id: device.id,
        action: 'config_updated',
        metadata: { config: mqttPayload },
      })
    } catch (_) { /* best-effort */ }

    return new Response(JSON.stringify({
      status: device.status,
      config: mqttPayload,
    }), {
      headers: { 'Content-Type': 'application/json' },
    })

  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500, headers: { 'Content-Type': 'application/json' },
    })
  }
})
