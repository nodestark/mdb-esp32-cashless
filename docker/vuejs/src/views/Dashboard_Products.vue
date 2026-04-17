<template>

<div class="p-6 space-y-6">

  <!-- HEADER -->
  <div class="flex justify-between items-center">
    <div>
      <h1 class="text-2xl font-bold text-gray-800">Products</h1>
      <p class="text-gray-500">Product catalog available for vending machines</p>
    </div>

    <button
      @click="showAddModal = true; createError = null"
      class="px-4 py-2 bg-slate-800 text-white rounded hover:bg-slate-700 transition"
    >
      + Add Product
    </button>
  </div>

  <!-- SEARCH -->
  <div>
    <input
      v-model="search"
      placeholder="Search by name..."
      class="w-full max-w-sm border rounded px-3 py-2 text-sm text-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-300"
    />
  </div>

  <!-- AI DIAGNOSIS -->
  <div class="bg-white rounded-xl shadow p-5">

    <div class="flex items-center justify-between mb-4">
      <div>
        <h2 class="font-semibold text-gray-700">AI Products Diagnosis</h2>
        <p class="text-xs text-gray-400 mt-0.5">Analysis of all products in the last 30 days</p>
      </div>
      <button
        @click="generateProductsDiagnosis"
        :disabled="diagnosisLoading"
        class="flex items-center gap-1.5 px-3 py-1.5 rounded-full text-sm font-medium bg-purple-100 text-purple-700 hover:bg-purple-200 transition disabled:opacity-50"
      >
        <svg class="w-4 h-4" :class="{ 'animate-spin': diagnosisLoading }" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
          <path v-if="!diagnosisLoading" stroke-linecap="round" stroke-linejoin="round" d="M9.813 15.904L9 18.75l-.813-2.846a4.5 4.5 0 00-3.09-3.09L2.25 12l2.846-.813a4.5 4.5 0 003.09-3.09L9 5.25l.813 2.846a4.5 4.5 0 003.09 3.09L15.75 12l-2.846.813a4.5 4.5 0 00-3.09 3.09z"/>
          <path v-else stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0l3.181 3.183a8.25 8.25 0 0013.803-3.7M4.031 9.865a8.25 8.25 0 0113.803-3.7l3.181 3.182m0-4.991v4.99"/>
        </svg>
        {{ diagnosisLoading ? 'Analyzing...' : diagnosisText ? 'Refresh' : 'Generate Diagnosis' }}
      </button>
    </div>

    <div v-if="!diagnosisText && !diagnosisLoading && !diagnosisError" class="text-center py-8 text-gray-400 text-sm">
      Click "Generate Diagnosis" to get an AI analysis of your product catalog.
    </div>
    <div v-if="diagnosisLoading" class="text-sm text-gray-500 py-4 text-center">
      Collecting product data and generating insights...
    </div>
    <p v-if="diagnosisError" class="text-sm text-red-500">{{ diagnosisError }}</p>
    <p v-if="diagnosisText" class="text-sm text-gray-700 leading-relaxed whitespace-pre-wrap">{{ diagnosisText }}</p>

  </div>

  <!-- TABLE -->
  <div class="bg-white rounded-xl shadow overflow-hidden">

    <table class="w-full text-sm">

      <thead class="bg-gray-50">
        <tr class="text-left text-gray-600">
          <th class="p-4">Image</th>
          <th class="p-4">Name</th>
          <th class="p-4">Price</th>
          <th class="p-4 w-28 text-center">Warehouse</th>
          <th class="p-4">Actions</th>
        </tr>
      </thead>

      <tbody>

        <tr
          v-for="product in filteredProducts"
          :key="product.id"
          class="border-t hover:bg-gray-50"
        >

          <td class="p-4">
            <img
              v-if="product.image_url"
              :src="product.image_url"
              class="w-12 h-12 object-cover rounded"
            />
            <div v-else class="w-12 h-12 rounded bg-gray-100"></div>
          </td>

          <td class="p-4 font-medium text-gray-700">{{ product.name }}</td>

          <td class="p-4 font-medium text-green-600">{{ formatCurrency(product.price) }}</td>

          <td class="p-4 text-center">
            <span
              class="text-sm font-semibold"
              :class="(product.current_stock ?? 0) === 0 ? 'text-red-600' : (product.current_stock ?? 0) <= 10 ? 'text-orange-500' : 'text-gray-700'"
            >
              {{ product.current_stock ?? 0 }}
            </span>
          </td>

          <td class="p-4 flex gap-2 items-center">
            <button
              @click="openStockModal(product)"
              class="px-2 py-1 text-sm bg-emerald-600 hover:bg-emerald-500 text-white rounded transition"
            >
              + Stock
            </button>
            <button
              @click="editProduct(product)"
              class="px-2 py-1 text-sm bg-yellow-500 hover:bg-yellow-400 text-white rounded transition"
            >
              Edit
            </button>
            <button
              @click="confirmDisable(product)"
              class="px-2 py-1 text-sm border border-red-300 hover:bg-red-50 text-red-600 rounded transition"
            >
              Disable
            </button>
          </td>

        </tr>

        <tr v-if="!loading && filteredProducts.length === 0">
          <td colspan="5" class="p-8 text-center text-gray-400 text-sm">
            {{ search ? 'No products match your search.' : 'No products yet. Add your first product.' }}
          </td>
        </tr>

      </tbody>

    </table>

    <div v-if="loading" class="p-6 text-center text-gray-500 text-sm">
      Loading products...
    </div>

  </div>

</div>

<!-- ADD PRODUCT MODAL -->

<div
  v-if="showAddModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>

  <div class="bg-white p-6 rounded-xl w-96">

    <h2 class="text-lg font-semibold mb-4">
      Add Product
    </h2>

    <input
      v-model="newProductName"
      placeholder="Product name"
      class="w-full border rounded p-2 mb-3"
    />

    <input
      v-model="newProductPrice"
      type="number"
      placeholder="Price"
      min="0.01"
      step="0.01"
      class="w-full border rounded p-2 mb-3"
    />

    <input
      type="file"
      accept="image/*"
      @change="handleImage"
      class="mb-4"
    />

    <p v-if="createError" class="text-red-500 text-sm mb-3">{{ createError }}</p>

    <div class="flex justify-end gap-2">

      <button
        @click="showAddModal=false"
        :disabled="createSaving"
        class="px-3 py-1 border rounded disabled:opacity-50"
      >
        Cancel
      </button>

      <button
        @click="createProduct"
        :disabled="createSaving"
        class="px-3 py-1 bg-blue-600 text-white rounded disabled:opacity-50"
      >
        {{ createSaving ? 'Creating...' : 'Create' }}
      </button>

    </div>

  </div>

</div>

<!-- EDIT PRODUCT MODAL -->

<div
  v-if="showEditModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>

  <div class="bg-white p-6 rounded-xl w-96">

    <h2 class="text-lg font-semibold mb-4">
      Edit Product
    </h2>

    <label class="block text-sm text-gray-600 mb-1">Name</label>
    <input
      v-model="editProductName"
      placeholder="Product name"
      class="w-full border rounded p-2 mb-3"
    />

    <label class="block text-sm text-gray-600 mb-1">Price</label>
    <input
      v-model="editProductPrice"
      type="number"
      placeholder="Price"
      min="0.01"
      step="0.01"
      class="w-full border rounded p-2 mb-3"
    />

    <label class="block text-sm text-gray-600 mb-1">Warehouse Stock</label>
    <input
      v-model.number="editProductStock"
      type="number"
      placeholder="Quantity"
      min="0"
      step="1"
      class="w-full border rounded p-2 mb-3"
    />

    <label class="block text-sm text-gray-600 mb-1">Image</label>
    <input
      type="file"
      accept="image/*"
      @change="handleEditImage"
      class="mb-4"
    />

    <p v-if="editError" class="text-red-500 text-sm mb-3">{{ editError }}</p>

    <div class="flex justify-end gap-2">

      <button
        @click="showEditModal=false"
        :disabled="editSaving"
        class="px-3 py-1 border rounded disabled:opacity-50"
      >
        Cancel
      </button>

      <button
        @click="updateProduct"
        :disabled="editSaving"
        class="px-3 py-1 bg-yellow-500 text-white rounded disabled:opacity-50"
      >
        {{ editSaving ? 'Saving...' : 'Save' }}
      </button>

    </div>

  </div>

</div>

<!-- ADD STOCK MODAL -->

<div
  v-if="showStockModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>
  <div class="bg-white p-6 rounded-xl w-80">

    <h2 class="text-lg font-semibold mb-1">Add Stock</h2>
    <p class="text-sm text-gray-500 mb-4">{{ stockProduct?.name }}</p>

    <input
      v-model.number="stockAmount"
      type="number"
      placeholder="Quantity received"
      min="1"
      step="1"
      class="w-full border rounded p-2 mb-4"
    />

    <div class="flex justify-end gap-2">
      <button @click="showStockModal = false" class="px-3 py-1 border rounded">Cancel</button>
      <button @click="addStock" class="px-3 py-1 bg-emerald-600 text-white rounded">Add</button>
    </div>

  </div>
</div>

<!-- CONFIRM DISABLE MODAL -->

<div
  v-if="showConfirmModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>

  <div class="bg-white p-6 rounded-xl w-80">

    <h2 class="text-lg font-semibold mb-2">Disable Product</h2>

    <p class="text-gray-600 mb-6">
      Are you sure you want to disable <span class="font-medium text-gray-900">{{ productToDisable?.name }}</span>?
    </p>

    <div class="flex justify-end gap-2">

      <button
        @click="showConfirmModal = false"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="disableProduct"
        class="px-3 py-1 bg-red-600 text-white rounded"
      >
        Disable
      </button>

    </div>

  </div>

</div>

</template>

<script setup>

import { ref, computed, onMounted } from "vue"
import { supabase } from "@/lib/supabase"

const products = ref([])
const loading = ref(false)
const search = ref("")

const showAddModal = ref(false)
const showEditModal = ref(false)
const showConfirmModal = ref(false)
const productToDisable = ref(null)

const showStockModal = ref(false)
const stockProduct = ref(null)
const stockAmount = ref(null)

const newProductName = ref("")
const newProductPrice = ref("")
const productImage = ref(null)
const createSaving = ref(false)
const createError = ref(null)

const editingProduct = ref(null)
const editProductName = ref("")
const editProductPrice = ref("")
const editProductStock = ref(0)
const editProductImage = ref(null)
const editSaving = ref(false)
const editError = ref(null)

const filteredProducts = computed(() => {
  if (!search.value.trim()) return products.value
  const term = search.value.trim().toLowerCase()
  return products.value.filter(p => p.name?.toLowerCase().includes(term))
})

function formatCurrency(value) {
  return new Intl.NumberFormat("pt-BR", { style: "currency", currency: "BRL" }).format(value ?? 0)
}

function handleImage(e) {
  productImage.value = e.target.files[0]
}

function handleEditImage(e) {
  editProductImage.value = e.target.files[0]
}

function editProduct(product) {
  editingProduct.value = product
  editProductName.value = product.name
  editProductPrice.value = product.price
  editProductStock.value = product.current_stock ?? 0
  editProductImage.value = null
  editError.value = null
  showEditModal.value = true
}

async function loadProducts() {
  loading.value = true

  const { data } = await supabase
    .from("products")
    .select("*")
    .eq("enabled", true)
    .order("name")

  products.value = data ?? []
  loading.value = false
}

async function uploadImage(file) {
  const ext = file.name.split(".").pop() || "jpg"
  const fileName = `${crypto.randomUUID()}.${ext}`

  const { error } = await supabase.storage
    .from("product-images")
    .upload(fileName, file)

  if (error) throw error

  const { data } = supabase.storage.from("product-images").getPublicUrl(fileName)
  return data.publicUrl
}

async function createProduct() {
  if (!newProductName.value.trim()) return
  if (!newProductPrice.value || Number(newProductPrice.value) <= 0) return

  createSaving.value = true
  createError.value = null

  try {
    let imageUrl = null
    if (productImage.value) imageUrl = await uploadImage(productImage.value)

    const { error } = await supabase.from("products").insert({
      name: newProductName.value.trim(),
      price: Number(newProductPrice.value),
      image_url: imageUrl
    })

    if (error) throw error

    showAddModal.value = false
    newProductName.value = ""
    newProductPrice.value = ""
    productImage.value = null
    loadProducts()
  } catch (err) {
    createError.value = err.message ?? "Failed to create product"
    console.error("createProduct error:", err)
  } finally {
    createSaving.value = false
  }
}

async function updateProduct() {
  if (!editProductName.value.trim()) return
  if (!editProductPrice.value || Number(editProductPrice.value) <= 0) return

  editSaving.value = true
  editError.value = null

  try {
    let imageUrl = editingProduct.value.image_url
    if (editProductImage.value) imageUrl = await uploadImage(editProductImage.value)

    const { error } = await supabase
      .from("products")
      .update({
        name: editProductName.value.trim(),
        price: Number(editProductPrice.value),
        current_stock: Math.max(0, editProductStock.value ?? 0),
        image_url: imageUrl
      })
      .eq("id", editingProduct.value.id)

    if (error) throw error

    showEditModal.value = false
    editingProduct.value = null
    loadProducts()
  } catch (err) {
    editError.value = err.message ?? "Failed to save product"
    console.error("updateProduct error:", err)
  } finally {
    editSaving.value = false
  }
}

function confirmDisable(product) {
  productToDisable.value = product
  showConfirmModal.value = true
}

function openStockModal(product) {
  stockProduct.value = product
  stockAmount.value = null
  showStockModal.value = true
}

async function addStock() {
  if (!stockAmount.value || stockAmount.value < 1) return

  await supabase
    .from("products")
    .update({ current_stock: (stockProduct.value.current_stock ?? 0) + stockAmount.value })
    .eq("id", stockProduct.value.id)

  showStockModal.value = false
  stockProduct.value = null
  stockAmount.value = null
  loadProducts()
}

async function disableProduct() {
  const product = productToDisable.value

  if (product.image_url) {
    const parts = product.image_url.split("/product-images/")
    if (parts[1]) {
      await supabase.storage.from("product-images").remove([parts[1]])
    }
  }

  await supabase
    .from("products")
    .update({ enabled: false, image_url: null })
    .eq("id", product.id)

  showConfirmModal.value = false
  productToDisable.value = null

  loadProducts()
}

// --- PRODUCTS GENERAL DIAGNOSIS ---

const diagnosisText = ref('')
const diagnosisLoading = ref(false)
const diagnosisError = ref('')
const diagnosisAbort = ref(null)

async function generateProductsDiagnosis() {
  if (diagnosisAbort.value) diagnosisAbort.value.abort()

  diagnosisLoading.value = true
  diagnosisError.value = ''
  diagnosisText.value = ''

  const abort = new AbortController()
  diagnosisAbort.value = abort

  try {
    const { data: cred } = await supabase
      .from('credentials')
      .select('value')
      .eq('key', 'openai_api_key')
      .maybeSingle()

    if (!cred?.value) throw new Error('OpenAI API key not configured. Add it in Settings → API Keys.')

    const since = new Date()
    since.setDate(since.getDate() - 30)

    const [salesRes, coilsRes] = await Promise.all([
      supabase
        .from('sales')
        .select('product_id, item_price, products(name)')
        .gte('created_at', since.toISOString()),
      supabase
        .from('machine_coils')
        .select('product_id, current_stock, capacity')
    ])

    const sales = salesRes.data ?? []
    const coils = coilsRes.data ?? []

    const statsMap = {}
    for (const s of sales) {
      const id = s.product_id
      if (!id) continue
      if (!statsMap[id]) statsMap[id] = { name: s.products?.name ?? id, count: 0, revenue: 0 }
      statsMap[id].count++
      statsMap[id].revenue += s.item_price ?? 0
    }

    const productLines = products.value.map(p => {
      const s = statsMap[p.id] ?? { count: 0, revenue: 0 }
      const deployedIn = coils.filter(c => c.product_id === p.id).length
      const totalCoilStock = coils.filter(c => c.product_id === p.id).reduce((a, c) => a + (c.current_stock ?? 0), 0)
      return `  - ${p.name} | price: R$ ${p.price?.toFixed(2)} | warehouse: ${p.current_stock ?? 0} | machines: ${deployedIn} | sales: ${s.count} | revenue: R$ ${s.revenue.toFixed(2)} | coil stock: ${totalCoilStock}`
    }).join('\n')

    const totalRevenue = sales.reduce((s, r) => s + (r.item_price ?? 0), 0)
    const totalCount = sales.length

    const prompt = `You are a vending machine business analyst. Analyze the complete product catalog data below and provide a general diagnosis in Portuguese (Brazil).

OVERVIEW (last 30 days):
- Total products: ${products.value.length}
- Total transactions: ${totalCount}
- Total revenue: R$ ${totalRevenue.toFixed(2)}

PRODUCTS (name | price | warehouse stock | machines deployed | sales | revenue | coil stock):
${productLines}

Provide:
1. Overall assessment of the catalog performance
2. Best selling products and why they stand out
3. Products with low or zero sales — should they be reviewed or removed?
4. Stock situation — any products at risk of running out?
5. 3 to 5 prioritized recommendations to improve the catalog`

    const res = await fetch('https://api.openai.com/v1/chat/completions', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${cred.value}`
      },
      body: JSON.stringify({
        model: 'gpt-4o-mini',
        messages: [{ role: 'user', content: prompt }],
        temperature: 0.7
      }),
      signal: abort.signal
    })

    if (!res.ok) {
      const err = await res.json()
      throw new Error(err.error?.message ?? 'OpenAI request failed')
    }

    const json = await res.json()
    diagnosisText.value = json.choices[0].message.content

  } catch (err) {
    if (err.name === 'AbortError') return
    diagnosisError.value = err.message
    console.error('generateProductsDiagnosis error:', err)
  } finally {
    diagnosisAbort.value = null
    diagnosisLoading.value = false
  }
}

onMounted(loadProducts)

</script>
