import { createClient } from 'jsr:@supabase/supabase-js@2'

interface ImportProduct {
  name: string
  sellprice: number | null
  category_name: string | null
  image_url: string | null
}

interface ImportResult {
  total: number
  created: number
  skipped: number
  image_errors: number
  errors: string[]
}

Deno.serve(async (req) => {
  try {
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    // ── Auth + admin check ──────────────────────────────────────────────────
    const token = req.headers.get('Authorization')?.replace('Bearer ', '') ?? ''
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

    const companyId = membership.company_id

    // ── Parse request body ──────────────────────────────────────────────────
    const body = await req.json()
    const products: ImportProduct[] = body?.products
    if (!Array.isArray(products) || products.length === 0) {
      return new Response(JSON.stringify({ error: 'No products provided' }), {
        status: 400, headers: { 'Content-Type': 'application/json' },
      })
    }

    const result: ImportResult = {
      total: products.length,
      created: 0,
      skipped: 0,
      image_errors: 0,
      errors: [],
    }

    // ── Step 1: Ensure categories exist ─────────────────────────────────────
    const { data: existingCategories } = await adminClient
      .from('product_category')
      .select('id, name')
      .eq('company', companyId)

    const categoryMap = new Map<string, string>() // lowercase name → id
    for (const cat of existingCategories ?? []) {
      categoryMap.set(cat.name.toLowerCase(), cat.id)
    }

    // Collect unique new category names from the import
    const newCategoryNames = [
      ...new Set(
        products
          .map(p => p.category_name?.trim())
          .filter((n): n is string => !!n)
          .filter(name => !categoryMap.has(name.toLowerCase()))
      ),
    ]

    if (newCategoryNames.length > 0) {
      const { data: created, error: catError } = await adminClient
        .from('product_category')
        .insert(newCategoryNames.map(name => ({ name, company: companyId })))
        .select('id, name')

      if (catError) {
        result.errors.push(`Failed to create categories: ${catError.message}`)
      } else {
        for (const cat of created ?? []) {
          categoryMap.set(cat.name.toLowerCase(), cat.id)
        }
      }
    }

    // ── Step 2: Get existing product names to detect duplicates ─────────────
    const { data: existingProducts } = await adminClient
      .from('products')
      .select('name')
      .eq('company', companyId)

    const existingNames = new Set(
      (existingProducts ?? []).map((p: any) => (p.name ?? '').toLowerCase())
    )

    // ── Step 3: Create products + download images ───────────────────────────
    for (const p of products) {
      const name = p.name?.trim()
      if (!name) {
        result.skipped++
        continue
      }

      if (existingNames.has(name.toLowerCase())) {
        result.skipped++
        continue
      }

      // Insert product
      const categoryId = p.category_name
        ? categoryMap.get(p.category_name.trim().toLowerCase()) ?? null
        : null

      const { data: newProduct, error: insertError } = await adminClient
        .from('products')
        .insert({
          name,
          sellprice: p.sellprice ?? null,
          category: categoryId,
          company: companyId,
        })
        .select('id')
        .single()

      if (insertError) {
        result.errors.push(`Failed to create "${name}": ${insertError.message}`)
        continue
      }

      result.created++
      existingNames.add(name.toLowerCase()) // prevent duplicates within the same import batch

      // Download and upload image
      if (p.image_url) {
        try {
          const imgResponse = await fetch(p.image_url)
          if (!imgResponse.ok) throw new Error(`HTTP ${imgResponse.status}`)

          const contentType = imgResponse.headers.get('content-type') ?? 'image/jpeg'
          let ext = 'jpg'
          if (contentType.includes('png')) ext = 'png'
          else if (contentType.includes('webp')) ext = 'webp'

          const arrayBuffer = await imgResponse.arrayBuffer()
          const path = `${newProduct.id}.${ext}`

          const { error: uploadError } = await adminClient.storage
            .from('product-images')
            .upload(path, arrayBuffer, {
              contentType,
              upsert: true,
            })

          if (uploadError) throw uploadError

          // Save path to product
          await adminClient
            .from('products')
            .update({ image_path: path })
            .eq('id', newProduct.id)
        } catch (imgErr: any) {
          result.image_errors++
          result.errors.push(`Image failed for "${name}": ${imgErr?.message ?? imgErr}`)
          // Product still created, just without image
        }
      }
    }

    return new Response(JSON.stringify(result), {
      status: 200, headers: { 'Content-Type': 'application/json' },
    })
  } catch (err: any) {
    return new Response(JSON.stringify({ error: err?.message ?? err }), {
      status: 500, headers: { 'Content-Type': 'application/json' },
    })
  }
})
