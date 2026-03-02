import { createClient } from 'jsr:@supabase/supabase-js@2'
import { sendPushToUsers } from '../_shared/web-push.ts'

Deno.serve(async (req) => {
  // CORS preflight
  if (req.method === 'OPTIONS') {
    return new Response(null, {
      headers: {
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Methods': 'POST, OPTIONS',
        'Access-Control-Allow-Headers': 'Authorization, Content-Type, apikey, x-client-info',
      },
    })
  }

  try {
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!,
    )

    // Verify user identity
    const token = req.headers.get('Authorization')?.replace('Bearer ', '') ?? ''
    const { data: { user }, error: userError } = await adminClient.auth.getUser(token)
    if (userError || !user) {
      return new Response(JSON.stringify({ error: 'unauthorized' }), {
        status: 401,
        headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' },
      })
    }

    // Get user's company
    const { data: membership } = await adminClient
      .from('organization_members')
      .select('company_id')
      .eq('user_id', user.id)
      .maybeSingle()

    if (!membership) {
      return new Response(JSON.stringify({ error: 'no organization' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' },
      })
    }

    // Debug: check each step of the push pipeline
    const debug: Record<string, unknown> = {}

    debug.vapid = {
      publicKey: !!Deno.env.get('VAPID_PUBLIC_KEY'),
      privateKey: !!Deno.env.get('VAPID_PRIVATE_KEY'),
      subject: !!Deno.env.get('VAPID_SUBJECT'),
    }

    debug.userId = user.id
    debug.companyId = membership.company_id

    const { data: allSubs, error: subsError } = await adminClient
      .from('push_subscriptions')
      .select('id, endpoint, user_id')
    debug.subscriptions = { count: allSubs?.length ?? 0, error: subsError?.message ?? null }

    const { data: members, error: membersError } = await adminClient
      .from('organization_members')
      .select('user_id')
      .eq('company_id', membership.company_id)
    debug.members = { count: members?.length ?? 0, userIds: members?.map((m: { user_id: string }) => m.user_id) ?? [], error: membersError?.message ?? null }

    if (allSubs && allSubs.length > 0) {
      debug.subUserIds = allSubs.map((s: { user_id: string }) => s.user_id)
      const memberIds = new Set(members?.map((m: { user_id: string }) => m.user_id) ?? [])
      debug.matchingAfterFilter = allSubs.filter((s: { user_id: string }) => memberIds.has(s.user_id)).length
    }

    // Send a test notification via the full push flow
    const result = await sendPushToUsers(adminClient, membership.company_id, 'sale', {
      title: '🔔 Test Notification',
      body: 'Push notifications are working! This is a test from VMflow.',
      data: { type: 'test' },
    })

    return new Response(JSON.stringify({ ok: true, ...result, debug }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' },
    })
  } catch (err) {
    return new Response(JSON.stringify({ error: err?.message ?? String(err) }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' },
    })
  }
})
