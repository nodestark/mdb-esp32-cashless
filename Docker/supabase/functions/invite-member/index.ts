import { createClient } from 'jsr:@supabase/supabase-js@2'

Deno.serve(async (req) => {
  try {
    const body = await req.json()
    const { email, role } = body

    if (!email || typeof email !== 'string') {
      return new Response(JSON.stringify({ error: 'email is required' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    if (!role || !['admin', 'viewer'].includes(role)) {
      return new Response(JSON.stringify({ error: 'role must be admin or viewer' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Admin client — privileged writes + identity resolution
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    const token = req.headers.get('Authorization')?.replace('Bearer ', '') ?? ''
    const { data: { user }, error: userError } = await adminClient.auth.getUser(token)
    if (userError || !user) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), {
        status: 401,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Verify caller is admin in their org
    const { data: membership, error: memberError } = await adminClient
      .from('organization_members')
      .select('company_id, role')
      .eq('user_id', user.id)
      .maybeSingle()

    if (memberError) throw memberError

    if (!membership) {
      return new Response(JSON.stringify({ error: 'User does not belong to an organization' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    if (membership.role !== 'admin') {
      return new Response(JSON.stringify({ error: 'Only admins can invite members' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    const expiresAt = new Date(Date.now() + 7 * 24 * 60 * 60 * 1000).toISOString()

    // Upsert invitation — on conflict reset token, expiry, and accepted_at
    const { data: invitation, error: inviteError } = await adminClient
      .from('invitations')
      .upsert(
        {
          company_id: membership.company_id,
          email: email.toLowerCase(),
          role,
          token: crypto.randomUUID(),
          expires_at: expiresAt,
          invited_by: user.id,
          accepted_at: null,
        },
        { onConflict: 'company_id,email' }
      )
      .select('token')
      .single()

    if (inviteError) throw inviteError

    return new Response(JSON.stringify({ token: invitation.token }), {
      status: 201,
      headers: { 'Content-Type': 'application/json' },
    })
  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500,
      headers: { 'Content-Type': 'application/json' },
    })
  }
})
