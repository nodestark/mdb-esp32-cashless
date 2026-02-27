import { createClient } from 'jsr:@supabase/supabase-js@2'

Deno.serve(async (req) => {
  try {
    const body = await req.json()
    const { name } = body

    if (!name || typeof name !== 'string' || name.trim() === '') {
      return new Response(JSON.stringify({ error: 'name is required' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Admin client — privileged writes + identity resolution
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    // Resolve caller identity by passing the JWT to the auth server for verification
    // (avoids local ES256 CryptoKey issue in Deno edge runtime)
    const token = req.headers.get('Authorization')?.replace('Bearer ', '') ?? ''
    const { data: { user }, error: userError } = await adminClient.auth.getUser(token)
    if (userError || !user) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), {
        status: 401,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Check the caller has no existing organization membership
    const { data: existingMember } = await adminClient
      .from('organization_members')
      .select('id')
      .eq('user_id', user.id)
      .maybeSingle()

    if (existingMember) {
      return new Response(JSON.stringify({ error: 'User already belongs to an organization' }), {
        status: 409,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Insert company
    const { data: company, error: companyError } = await adminClient
      .from('companies')
      .insert({ name: name.trim() })
      .select('id, name, created_at')
      .single()

    if (companyError) throw companyError

    // Insert organization_members row (admin role)
    const { error: memberError } = await adminClient
      .from('organization_members')
      .insert({ company_id: company.id, user_id: user.id, role: 'admin' })

    if (memberError) throw memberError

    // Update public.users.company
    const { error: userUpdateError } = await adminClient
      .from('users')
      .update({ company: company.id })
      .eq('id', user.id)

    if (userUpdateError) throw userUpdateError

    return new Response(JSON.stringify({ organization: company }), {
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
