<template>

<div class="p-6 space-y-6">

  <!-- HEADER -->
  <div class="flex justify-between items-center">
    <div>
      <h1 class="text-2xl font-bold text-gray-800">Products</h1>
      <p class="text-gray-500">Product catalog available for vending machines</p>
    </div>

    <button
      @click="showAddModal = true"
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

  <!-- TABLE -->
  <div class="bg-white rounded-xl shadow overflow-hidden">

    <table class="w-full text-sm">

      <thead class="bg-gray-50">
        <tr class="text-left text-gray-600">
          <th class="p-4">Image</th>
          <th class="p-4">Name</th>
          <th class="p-4">Price</th>
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

          <td class="p-4 flex gap-2">
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
          <td colspan="4" class="p-8 text-center text-gray-400 text-sm">
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

    <div class="flex justify-end gap-2">

      <button
        @click="showAddModal=false"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="createProduct"
        class="px-3 py-1 bg-blue-600 text-white rounded"
      >
        Create
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

    <input
      v-model="editProductName"
      placeholder="Product name"
      class="w-full border rounded p-2 mb-3"
    />

    <input
      v-model="editProductPrice"
      type="number"
      placeholder="Price"
      min="0.01"
      step="0.01"
      class="w-full border rounded p-2 mb-3"
    />

    <input
      type="file"
      accept="image/*"
      @change="handleEditImage"
      class="mb-4"
    />

    <div class="flex justify-end gap-2">

      <button
        @click="showEditModal=false"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="updateProduct"
        class="px-3 py-1 bg-yellow-500 text-white rounded"
      >
        Save
      </button>

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

const newProductName = ref("")
const newProductPrice = ref("")
const productImage = ref(null)

const editingProduct = ref(null)
const editProductName = ref("")
const editProductPrice = ref("")
const editProductImage = ref(null)

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
  editProductImage.value = null
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
  const fileName = `${crypto.randomUUID()}.jpg`

  const { error } = await supabase
    .storage
    .from("product-images")
    .upload(fileName, file)

  if (error) throw error

  const { data } = supabase
    .storage
    .from("product-images")
    .getPublicUrl(fileName)

  return data.publicUrl
}

async function createProduct() {
  if (!newProductName.value.trim()) return
  if (!newProductPrice.value || Number(newProductPrice.value) <= 0) return

  let imageUrl = null
  if (productImage.value) imageUrl = await uploadImage(productImage.value)

  await supabase.from("products").insert({
    name: newProductName.value.trim(),
    price: Number(newProductPrice.value),
    image_url: imageUrl
  })

  showAddModal.value = false
  newProductName.value = ""
  newProductPrice.value = ""
  productImage.value = null

  loadProducts()
}

async function updateProduct() {
  if (!editProductName.value.trim()) return
  if (!editProductPrice.value || Number(editProductPrice.value) <= 0) return

  let imageUrl = editingProduct.value.image_url
  if (editProductImage.value) imageUrl = await uploadImage(editProductImage.value)

  await supabase
    .from("products")
    .update({
      name: editProductName.value.trim(),
      price: Number(editProductPrice.value),
      image_url: imageUrl
    })
    .eq("id", editingProduct.value.id)

  showEditModal.value = false
  editingProduct.value = null

  loadProducts()
}

function confirmDisable(product) {
  productToDisable.value = product
  showConfirmModal.value = true
}

async function disableProduct() {
  await supabase
    .from("products")
    .update({ enabled: false })
    .eq("id", productToDisable.value.id)

  showConfirmModal.value = false
  productToDisable.value = null

  loadProducts()
}

onMounted(loadProducts)

</script>
