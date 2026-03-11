<template>

<div class="p-6">

  <div class="flex justify-between items-center mb-6">
    <h1 class="text-2xl font-bold">Products</h1>

    <button
      @click="showAddModal = true"
      class="px-4 py-2 bg-slate-800 text-white rounded"
    >
      + Add Product
    </button>
  </div>

  <table class="w-full border rounded-xl overflow-hidden">

    <thead class="bg-gray-100 text-left">
      <tr>
        <th class="p-3">Image</th>
        <th class="p-3">Name</th>
        <th class="p-3">Price</th>
        <th class="p-3">Actions</th>
      </tr>
    </thead>

    <tbody>

      <tr
        v-for="product in products"
        :key="product.id"
        class="border-t"
      >

        <td class="p-3">
          <img
            v-if="product.image_url"
            :src="product.image_url"
            class="w-12 h-12 object-cover rounded"
          />
        </td>

        <td class="p-3">
          {{ product.name }}
        </td>

        <td class="p-3">
          {{ product.price }}
        </td>
        
        <td class="p-3 flex gap-2">

        <button
          @click="editProduct(product)"
          class="px-2 py-1 text-sm bg-yellow-500 text-white rounded"
        >
        Edit
        </button>

        <button
          @click="confirmDisable(product)"
          class="px-2 py-1 text-sm bg-red-600 text-white rounded"
        >
        Disable
        </button>

        </td>
      </tr>

    </tbody>

  </table>

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
      class="w-full border rounded p-2 mb-3"
    />

    <input
      type="file"
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
      class="w-full border rounded p-2 mb-3"
    />

    <input
      type="file"
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

import { ref, onMounted } from "vue"
import { supabase } from "@/lib/supabase"

const products = ref([])

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

function handleImage(e){
  productImage.value = e.target.files[0]
}

function handleEditImage(e){
  editProductImage.value = e.target.files[0]
}

function editProduct(product){
  editingProduct.value = product
  editProductName.value = product.name
  editProductPrice.value = product.price
  editProductImage.value = null
  showEditModal.value = true
}

async function loadProducts(){

  const { data } = await supabase
    .from("products")
    .select("*")
    .eq("enabled", true)
    .order("created_at")

  products.value = data
}

async function uploadImage(file){

  const fileName = `${crypto.randomUUID()}.jpg`

  const { error } = await supabase
    .storage
    .from("product-images")
    .upload(fileName, file)

  if(error) throw error

  const { data } = supabase
    .storage
    .from("product-images")
    .getPublicUrl(fileName)

  return data.publicUrl
}

async function createProduct(){

  let imageUrl = null

  if(productImage.value){
    imageUrl = await uploadImage(productImage.value)
  }

  await supabase
    .from("products")
    .insert({
      name: newProductName.value,
      price: newProductPrice.value,
      image_url: imageUrl
    })

  showAddModal.value = false
  newProductName.value = ""
  newProductPrice.value = ""
  productImage.value = null

  loadProducts()
}

async function updateProduct(){

  let imageUrl = editingProduct.value.image_url

  if(editProductImage.value){
    imageUrl = await uploadImage(editProductImage.value)
  }

  await supabase
    .from("products")
    .update({
      name: editProductName.value,
      price: editProductPrice.value,
      image_url: imageUrl
    })
    .eq("id", editingProduct.value.id)

  showEditModal.value = false
  editingProduct.value = null

  loadProducts()
}

function confirmDisable(product){
  productToDisable.value = product
  showConfirmModal.value = true
}

async function disableProduct(){

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