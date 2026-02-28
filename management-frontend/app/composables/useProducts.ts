import { useSupabaseClient } from '#imports'

interface ProductCategory {
  id: string
  name: string
}

interface Product {
  id: string
  name: string
  sellprice: number | null
  description: string | null
  category: string | null
  category_name?: string | null
  image_path: string | null
  image_url: string | null
}

export function getProductImageUrl(path: string): string {
  const config = useRuntimeConfig()
  const supabaseUrl = config.public.supabase?.url || 'http://127.0.0.1:54321'
  return `${supabaseUrl}/storage/v1/object/public/product-images/${path}`
}

export function useProducts() {
  const products = useState<Product[]>('products', () => [])
  const categories = useState<ProductCategory[]>('product-categories', () => [])
  const loading = ref(false)

  async function fetchProducts() {
    loading.value = true
    try {
      const supabase = useSupabaseClient()
      const [productsRes, categoriesRes] = await Promise.all([
        supabase
          .from('products')
          .select('id, name, sellprice, description, category, image_path, product_category(name)')
          .order('name'),
        supabase
          .from('product_category')
          .select('id, name')
          .order('name'),
      ])

      if (productsRes.error) throw productsRes.error
      if (categoriesRes.error) throw categoriesRes.error

      products.value = ((productsRes.data ?? []) as any[]).map((p) => ({
        id: p.id,
        name: p.name,
        sellprice: p.sellprice,
        description: p.description,
        category: p.category,
        category_name: p.product_category?.name ?? null,
        image_path: p.image_path ?? null,
        image_url: p.image_path ? getProductImageUrl(p.image_path) : null,
      }))

      categories.value = (categoriesRes.data ?? []) as ProductCategory[]
    } finally {
      loading.value = false
    }
  }

  async function createProduct(product: { name: string; sellprice: number | null; description: string | null; category: string | null; company: string }) {
    const supabase = useSupabaseClient()
    const { data, error } = await supabase.from('products').insert(product).select('id').single()
    if (error) throw error
    const newId = (data as any).id as string
    await fetchProducts()
    return newId
  }

  async function updateProduct(id: string, updates: { name?: string; sellprice?: number | null; description?: string | null; category?: string | null }) {
    const supabase = useSupabaseClient()
    const { error } = await supabase.from('products').update(updates).eq('id', id)
    if (error) throw error
    await fetchProducts()
  }

  async function deleteProduct(id: string) {
    const supabase = useSupabaseClient()
    // Remove image from storage if exists
    const product = products.value.find(p => p.id === id)
    if (product?.image_path) {
      await supabase.storage.from('product-images').remove([product.image_path])
    }
    const { error } = await supabase.from('products').delete().eq('id', id)
    if (error) throw error
    await fetchProducts()
  }

  async function uploadProductImage(productId: string, file: File) {
    const supabase = useSupabaseClient()
    const ext = file.name.split('.').pop()?.toLowerCase() || 'jpg'
    const path = `${productId}.${ext}`

    // Upload (upsert to overwrite existing)
    const { error: uploadError } = await supabase.storage
      .from('product-images')
      .upload(path, file, { upsert: true })
    if (uploadError) throw uploadError

    // Save path to products table
    const { error: updateError } = await supabase
      .from('products')
      .update({ image_path: path })
      .eq('id', productId)
    if (updateError) throw updateError

    await fetchProducts()
  }

  async function deleteProductImage(productId: string, path: string) {
    const supabase = useSupabaseClient()
    await supabase.storage.from('product-images').remove([path])
    const { error } = await supabase
      .from('products')
      .update({ image_path: null })
      .eq('id', productId)
    if (error) throw error
    await fetchProducts()
  }

  async function createCategory(category: { name: string; company: string }) {
    const supabase = useSupabaseClient()
    const { error } = await supabase.from('product_category').insert(category)
    if (error) throw error
    await fetchProducts()
  }

  async function deleteCategory(id: string) {
    const supabase = useSupabaseClient()
    const { error } = await supabase.from('product_category').delete().eq('id', id)
    if (error) throw error
    await fetchProducts()
  }

  return { products, categories, loading, fetchProducts, createProduct, updateProduct, deleteProduct, uploadProductImage, deleteProductImage, createCategory, deleteCategory }
}
