import { createClient } from 'jsr:@supabase/supabase-js@2'

Deno.serve(async (req) => {
  try {
    // ── Authenticate the caller via their JWT ────────────────────────────
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

    // Service-role client for storage and cross-table operations
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!,
    )

    // Verify admin role
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

    const companyId = membership.company_id

    // ── Parse request body ───────────────────────────────────────────────
    const body = await req.json()
    const { tag, asset_name, version_label, notes } = body as {
      tag: string
      asset_name: string
      version_label?: string
      notes?: string
    }

    if (!tag || !asset_name) {
      return new Response(JSON.stringify({ error: 'tag and asset_name are required' }), {
        status: 400, headers: { 'Content-Type': 'application/json' },
      })
    }

    const label = version_label ?? tag

    // ── Check if already imported ────────────────────────────────────────
    const { data: existing } = await adminClient
      .from('firmware_versions')
      .select('id')
      .eq('company_id', companyId)
      .eq('source_tag', tag)
      .maybeSingle()

    if (existing) {
      return new Response(JSON.stringify({ error: 'This release has already been imported' }), {
        status: 409, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Fetch GitHub release by tag ──────────────────────────────────────
    const repo = Deno.env.get('GITHUB_FIRMWARE_REPO')
    if (!repo) {
      return new Response(JSON.stringify({ error: 'GITHUB_FIRMWARE_REPO not configured' }), {
        status: 500, headers: { 'Content-Type': 'application/json' },
      })
    }

    const releaseRes = await fetch(
      `https://api.github.com/repos/${repo}/releases/tags/${encodeURIComponent(tag)}`,
      { headers: { Accept: 'application/vnd.github.v3+json', 'User-Agent': 'vmflow-edge-function' } }
    )
    if (!releaseRes.ok) {
      const text = await releaseRes.text()
      return new Response(JSON.stringify({ error: `GitHub API error: ${releaseRes.status} ${text}` }), {
        status: 502, headers: { 'Content-Type': 'application/json' },
      })
    }

    const release = await releaseRes.json()

    // Find the requested asset
    const asset = release.assets?.find((a: { name: string }) => a.name === asset_name)
    if (!asset) {
      return new Response(JSON.stringify({ error: `Asset "${asset_name}" not found in release ${tag}` }), {
        status: 404, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Download the .bin asset from GitHub ──────────────────────────────
    const downloadRes = await fetch(asset.browser_download_url, {
      headers: { 'User-Agent': 'vmflow-edge-function' },
    })
    if (!downloadRes.ok) {
      return new Response(JSON.stringify({ error: `Failed to download asset: ${downloadRes.status}` }), {
        status: 502, headers: { 'Content-Type': 'application/json' },
      })
    }

    const binaryData = new Uint8Array(await downloadRes.arrayBuffer())
    const filePath = `${companyId}/${label}.bin`

    // ── Upload to Supabase storage ───────────────────────────────────────
    const { error: uploadError } = await adminClient.storage
      .from('firmware')
      .upload(filePath, binaryData, {
        upsert: true,
        contentType: 'application/octet-stream',
      })

    if (uploadError) {
      return new Response(JSON.stringify({ error: `Storage upload failed: ${uploadError.message}` }), {
        status: 500, headers: { 'Content-Type': 'application/json' },
      })
    }

    // ── Create firmware_versions record ──────────────────────────────────
    const { data: newVersion, error: insertError } = await adminClient
      .from('firmware_versions')
      .insert({
        company_id: companyId,
        version_label: label,
        file_path: filePath,
        file_size: binaryData.length,
        notes: notes || release.body?.slice(0, 500) || null,
        uploaded_by: user.id,
        source_type: 'github',
        source_tag: tag,
      })
      .select()
      .single()

    if (insertError) {
      // Clean up storage on DB failure
      await adminClient.storage.from('firmware').remove([filePath])
      return new Response(JSON.stringify({ error: `DB insert failed: ${insertError.message}` }), {
        status: 500, headers: { 'Content-Type': 'application/json' },
      })
    }

    return new Response(JSON.stringify(newVersion), {
      status: 200, headers: { 'Content-Type': 'application/json' },
    })
  } catch (err: unknown) {
    const message = err instanceof Error ? err.message : String(err)
    return new Response(JSON.stringify({ error: message }), {
      status: 500, headers: { 'Content-Type': 'application/json' },
    })
  }
})
