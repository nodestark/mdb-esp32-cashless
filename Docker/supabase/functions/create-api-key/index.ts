import { createClient } from 'jsr:@supabase/supabase-js@2'

const CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'

function generateKey(): string {
  const bytes = new Uint8Array(48)
  crypto.getRandomValues(bytes)
  return 'vmf_' + Array.from(bytes).map(b => CHARS[b % CHARS.length]).join('')
}

async function hashKey(key: string): Promise<string> {
  const encoded = new TextEncoder().encode(key)
  const hash = await crypto.subtle.digest('SHA-256', encoded)
  return Array.from(new Uint8Array(hash)).map(b => b.toString(16).padStart(2, '0')).join('')
}

Deno.serve(async (req) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), {
      status: 405, headers: { 'Content-Type': 'application/json' },
    })
  }

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

    if (!membership || membership.role !== 'admin') {
      return new Response(JSON.stringify({ error: 'Admin role required' }), {
        status: 403, headers: { 'Content-Type': 'application/json' },
      })
    }

    const body = await req.json()
    const name = body?.name?.trim()
    if (!name) {
      return new Response(JSON.stringify({ error: 'name is required' }), {
        status: 400, headers: { 'Content-Type': 'application/json' },
      })
    }

    const key = generateKey()
    const keyHash = await hashKey(key)
    const keyPrefix = key.slice(0, 8)

    const { error: insertError } = await adminClient
      .from('api_keys')
      .insert({
        company_id: membership.company_id,
        created_by: user.id,
        name,
        key_prefix: keyPrefix,
        key_hash: keyHash,
      })

    if (insertError) throw insertError

    return new Response(JSON.stringify({ key, key_prefix: keyPrefix, name }), {
      status: 201, headers: { 'Content-Type': 'application/json' },
    })
  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500, headers: { 'Content-Type': 'application/json' },
    })
  }
})
