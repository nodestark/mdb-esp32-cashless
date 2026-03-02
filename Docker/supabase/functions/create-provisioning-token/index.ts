import { createClient } from 'jsr:@supabase/supabase-js@2'

// Unambiguous characters: no 0/O, 1/I/L
const CHARS = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789'

function generateShortCode(length = 8): string {
  const bytes = new Uint8Array(length)
  crypto.getRandomValues(bytes)
  return Array.from(bytes).map(b => CHARS[b % CHARS.length]).join('')
}

Deno.serve(async (req) => {
  try {
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    const token = req.headers.get('Authorization')?.replace('Bearer ', '') ?? ''
    const { data: { user }, error: userError } = await adminClient.auth.getUser(token)
    if (userError || !user) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), {
        status: 401, headers: { 'Content-Type': 'application/json' },
      })
    }

    // Verify caller is admin
    const { data: membership } = await adminClient
      .from('organization_members')
      .select('company_id, role')
      .eq('user_id', user.id)
      .maybeSingle()

    if (!membership) {
      return new Response(JSON.stringify({ error: 'No organization found' }), {
        status: 403, headers: { 'Content-Type': 'application/json' },
      })
    }
    if (membership.role !== 'admin') {
      return new Response(JSON.stringify({ error: 'Admin role required' }), {
        status: 403, headers: { 'Content-Type': 'application/json' },
      })
    }

    // Generate a unique short code (retry on collision, highly unlikely)
    let shortCode = ''
    for (let attempt = 0; attempt < 5; attempt++) {
      shortCode = generateShortCode()
      const { data: existing } = await adminClient
        .from('device_provisioning')
        .select('id')
        .eq('short_code', shortCode)
        .maybeSingle()
      if (!existing) break
    }

    // Parse optional fields from request body
    let deviceName: string | null = null
    let deviceOnly = false
    try {
      const body = await req.json()
      if (body?.name && typeof body.name === 'string' && body.name.trim()) {
        deviceName = body.name.trim()
      }
      if (body?.device_only === true) {
        deviceOnly = true
      }
    } catch {
      // No body or invalid JSON — defaults apply
    }

    const expiresAt = new Date(Date.now() + 60 * 60 * 1000).toISOString() // 1 hour

    const { data, error } = await adminClient
      .from('device_provisioning')
      .insert({
        company_id: membership.company_id,
        short_code: shortCode,
        expires_at: expiresAt,
        created_by: user.id,
        name: deviceName,
        device_only: deviceOnly,
      })
      .select('short_code, expires_at')
      .single()

    if (error) throw error

    return new Response(JSON.stringify(data), {
      status: 201, headers: { 'Content-Type': 'application/json' },
    })
  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500, headers: { 'Content-Type': 'application/json' },
    })
  }
})
