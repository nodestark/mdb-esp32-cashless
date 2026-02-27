import { createClient } from 'jsr:@supabase/supabase-js@2'

Deno.serve(async (req) => {
  try {
    const body = await req.json()
    const { token } = body

    if (!token || typeof token !== 'string') {
      return new Response(JSON.stringify({ error: 'token is required' }), {
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

    // Look up invitation
    const { data: invitation, error: inviteError } = await adminClient
      .from('invitations')
      .select('id, company_id, email, role, expires_at, accepted_at')
      .eq('token', token)
      .maybeSingle()

    if (inviteError) throw inviteError

    if (!invitation) {
      return new Response(JSON.stringify({ error: 'Invalid invitation token' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Validate email matches
    if (invitation.email.toLowerCase() !== (user.email ?? '').toLowerCase()) {
      return new Response(JSON.stringify({ error: 'This invitation was sent to a different email address' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Validate not expired
    if (new Date(invitation.expires_at) < new Date()) {
      return new Response(JSON.stringify({ error: 'Invitation has expired' }), {
        status: 410,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Validate not already accepted
    if (invitation.accepted_at) {
      return new Response(JSON.stringify({ error: 'Invitation already accepted' }), {
        status: 409,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Insert organization_members
    const { error: memberError } = await adminClient
      .from('organization_members')
      .insert({
        company_id: invitation.company_id,
        user_id: user.id,
        role: invitation.role,
      })

    if (memberError) throw memberError

    // Update public.users.company
    const { error: userUpdateError } = await adminClient
      .from('users')
      .update({ company: invitation.company_id })
      .eq('id', user.id)

    if (userUpdateError) throw userUpdateError

    // Mark invitation as accepted
    const { error: acceptError } = await adminClient
      .from('invitations')
      .update({ accepted_at: new Date().toISOString() })
      .eq('id', invitation.id)

    if (acceptError) throw acceptError

    return new Response(JSON.stringify({ role: invitation.role }), {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
    })
  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500,
      headers: { 'Content-Type': 'application/json' },
    })
  }
})
