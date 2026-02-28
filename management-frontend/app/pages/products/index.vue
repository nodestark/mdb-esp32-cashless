<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import AppSidebar from '@/components/AppSidebar.vue'
import SiteHeader from '@/components/SiteHeader.vue'
import { SidebarInset, SidebarProvider } from '@/components/ui/sidebar'
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'

const { organization, role } = useOrganization()
const { products, categories, loading, fetchProducts, createProduct, updateProduct, deleteProduct, uploadProductImage, deleteProductImage, createCategory, deleteCategory } = useProducts()

const isAdmin = computed(() => role.value === 'admin')

onMounted(async () => {
  await fetchProducts()
})

// Product modal state
const showProductModal = ref(false)
const editingProduct = ref<any>(null)
const productForm = ref({ name: '', sellprice: null as number | null, description: '', category: '' })
const productLoading = ref(false)
const productError = ref('')

// Image upload state
const imageFile = ref<File | null>(null)
const imagePreview = ref<string | null>(null)
const removeImage = ref(false)

function openAddProduct() {
  editingProduct.value = null
  productForm.value = { name: '', sellprice: null, description: '', category: '' }
  imageFile.value = null
  imagePreview.value = null
  removeImage.value = false
  productError.value = ''
  showProductModal.value = true
}

function openEditProduct(product: any) {
  editingProduct.value = product
  productForm.value = {
    name: product.name ?? '',
    sellprice: product.sellprice,
    description: product.description ?? '',
    category: product.category ?? '',
  }
  imageFile.value = null
  imagePreview.value = product.image_url ?? null
  removeImage.value = false
  productError.value = ''
  showProductModal.value = true
}

function onImageSelected(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  if (!file) return
  imageFile.value = file
  removeImage.value = false
  const reader = new FileReader()
  reader.onload = (e) => {
    imagePreview.value = e.target?.result as string
  }
  reader.readAsDataURL(file)
}

function clearImage() {
  imageFile.value = null
  imagePreview.value = null
  if (editingProduct.value?.image_path) {
    removeImage.value = true
  }
}

async function submitProduct() {
  if (!productForm.value.name.trim()) {
    productError.value = 'Name is required'
    return
  }
  productLoading.value = true
  productError.value = ''
  try {
    let productId: string
    if (editingProduct.value) {
      productId = editingProduct.value.id
      await updateProduct(productId, {
        name: productForm.value.name.trim(),
        sellprice: productForm.value.sellprice,
        description: productForm.value.description.trim() || null,
        category: productForm.value.category || null,
      })
    } else {
      productId = await createProduct({
        name: productForm.value.name.trim(),
        sellprice: productForm.value.sellprice,
        description: productForm.value.description.trim() || null,
        category: productForm.value.category || null,
        company: organization.value!.id,
      })
    }

    // Handle image changes
    if (removeImage.value && editingProduct.value?.image_path) {
      await deleteProductImage(productId, editingProduct.value.image_path)
    }
    if (imageFile.value) {
      await uploadProductImage(productId, imageFile.value)
    }

    showProductModal.value = false
  } catch (err: unknown) {
    productError.value = err instanceof Error ? err.message : 'Failed to save product'
  } finally {
    productLoading.value = false
  }
}

async function handleDeleteProduct(id: string) {
  try {
    await deleteProduct(id)
  } catch (err: unknown) {
    // silent
  }
}

// Category modal state
const showCategoryModal = ref(false)
const categoryName = ref('')
const categoryLoading = ref(false)
const categoryError = ref('')

function openAddCategory() {
  categoryName.value = ''
  categoryError.value = ''
  showCategoryModal.value = true
}

async function submitCategory() {
  if (!categoryName.value.trim()) {
    categoryError.value = 'Name is required'
    return
  }
  categoryLoading.value = true
  categoryError.value = ''
  try {
    await createCategory({
      name: categoryName.value.trim(),
      company: organization.value!.id,
    })
    showCategoryModal.value = false
  } catch (err: unknown) {
    categoryError.value = err instanceof Error ? err.message : 'Failed to create category'
  } finally {
    categoryLoading.value = false
  }
}

async function handleDeleteCategory(id: string) {
  try {
    await deleteCategory(id)
  } catch (err: unknown) {
    // silent
  }
}

function formatCurrency(amount: number | null | undefined) {
  if (amount == null) return '—'
  return new Intl.NumberFormat('en-US', { style: 'currency', currency: 'EUR' }).format(amount)
}
</script>

<template>
  <SidebarProvider
    :style="{
      '--sidebar-width': 'calc(var(--spacing) * 72)',
      '--header-height': 'calc(var(--spacing) * 12)',
    }"
  >
    <AppSidebar variant="inset" />
    <SidebarInset>
      <SiteHeader />
      <div class="flex flex-1 flex-col gap-6 p-4 md:p-6">
        <h1 class="text-2xl font-semibold">Products</h1>

        <div v-if="loading" class="text-muted-foreground">Loading…</div>

        <Tabs v-else default-value="products">
          <TabsList>
            <TabsTrigger value="products">Products</TabsTrigger>
            <TabsTrigger value="categories">Categories</TabsTrigger>
          </TabsList>

          <!-- Products tab -->
          <TabsContent value="products" class="mt-4">
            <div class="mb-4 flex items-center justify-between">
              <h2 class="text-base font-medium">All products</h2>
              <button
                v-if="isAdmin"
                class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
                @click="openAddProduct"
              >
                Add product
              </button>
            </div>

            <div v-if="products.length === 0" class="text-sm text-muted-foreground">No products yet.</div>
            <div v-else class="rounded-md border">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b bg-muted/50 text-left">
                    <th class="w-14 px-4 py-3 font-medium"></th>
                    <th class="px-4 py-3 font-medium">Name</th>
                    <th class="px-4 py-3 font-medium">Category</th>
                    <th class="px-4 py-3 font-medium">Price</th>
                    <th v-if="isAdmin" class="px-4 py-3 font-medium">Actions</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="product in products"
                    :key="product.id"
                    class="border-b last:border-0 hover:bg-muted/30 transition-colors"
                  >
                    <td class="px-4 py-2">
                      <img
                        v-if="product.image_url"
                        :src="product.image_url"
                        :alt="product.name"
                        class="h-10 w-10 rounded-md object-cover"
                      />
                      <div
                        v-else
                        class="flex h-10 w-10 items-center justify-center rounded-md bg-muted"
                      >
                        <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5 text-muted-foreground/50" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/></svg>
                      </div>
                    </td>
                    <td class="px-4 py-3 font-medium">{{ product.name }}</td>
                    <td class="px-4 py-3 text-muted-foreground">{{ product.category_name ?? '—' }}</td>
                    <td class="px-4 py-3">{{ formatCurrency(product.sellprice) }}</td>
                    <td v-if="isAdmin" class="px-4 py-3">
                      <div class="flex items-center gap-2">
                        <button
                          class="text-xs text-primary hover:underline"
                          @click="openEditProduct(product)"
                        >
                          Edit
                        </button>
                        <button
                          class="text-xs text-destructive hover:underline"
                          @click="handleDeleteProduct(product.id)"
                        >
                          Delete
                        </button>
                      </div>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </TabsContent>

          <!-- Categories tab -->
          <TabsContent value="categories" class="mt-4">
            <div class="mb-4 flex items-center justify-between">
              <h2 class="text-base font-medium">All categories</h2>
              <button
                v-if="isAdmin"
                class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
                @click="openAddCategory"
              >
                Add category
              </button>
            </div>

            <div v-if="categories.length === 0" class="text-sm text-muted-foreground">No categories yet.</div>
            <div v-else class="rounded-md border">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b bg-muted/50 text-left">
                    <th class="px-4 py-3 font-medium">Name</th>
                    <th v-if="isAdmin" class="px-4 py-3 font-medium">Actions</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="cat in categories"
                    :key="cat.id"
                    class="border-b last:border-0 hover:bg-muted/30 transition-colors"
                  >
                    <td class="px-4 py-3 font-medium">{{ cat.name }}</td>
                    <td v-if="isAdmin" class="px-4 py-3">
                      <button
                        class="text-xs text-destructive hover:underline"
                        @click="handleDeleteCategory(cat.id)"
                      >
                        Delete
                      </button>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </TabsContent>
        </Tabs>
      </div>

      <!-- Product modal -->
      <div
        v-if="showProductModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showProductModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-4 text-lg font-semibold">{{ editingProduct ? 'Edit product' : 'Add product' }}</h2>
          <form class="space-y-4" @submit.prevent="submitProduct">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="product-name">Name</label>
              <input
                id="product-name"
                v-model="productForm.name"
                type="text"
                required
                placeholder="Product name"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>

            <!-- Image upload -->
            <div class="space-y-1">
              <label class="text-sm font-medium">Image</label>
              <div v-if="imagePreview" class="relative inline-block">
                <img :src="imagePreview" alt="Preview" class="h-24 w-24 rounded-lg object-cover border" />
                <button
                  type="button"
                  class="absolute -right-2 -top-2 flex h-5 w-5 items-center justify-center rounded-full bg-destructive text-destructive-foreground text-xs shadow"
                  @click="clearImage"
                >
                  &times;
                </button>
              </div>
              <div v-else>
                <label
                  for="product-image"
                  class="flex h-24 w-full cursor-pointer items-center justify-center rounded-lg border-2 border-dashed border-muted-foreground/25 text-sm text-muted-foreground transition-colors hover:border-muted-foreground/50 hover:bg-muted/30"
                >
                  <div class="text-center">
                    <svg xmlns="http://www.w3.org/2000/svg" class="mx-auto mb-1 h-6 w-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" x2="12" y1="3" y2="15"/></svg>
                    <span>Click to upload</span>
                  </div>
                </label>
                <input
                  id="product-image"
                  type="file"
                  accept="image/png,image/jpeg,image/webp"
                  class="hidden"
                  @change="onImageSelected"
                />
              </div>
            </div>

            <div class="space-y-1">
              <label class="text-sm font-medium" for="product-price">Price</label>
              <input
                id="product-price"
                v-model.number="productForm.sellprice"
                type="number"
                step="0.01"
                min="0"
                placeholder="0.00"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="product-category">Category</label>
              <select
                id="product-category"
                v-model="productForm.category"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              >
                <option value="">None</option>
                <option v-for="cat in categories" :key="cat.id" :value="cat.id">{{ cat.name }}</option>
              </select>
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="product-description">Description</label>
              <textarea
                id="product-description"
                v-model="productForm.description"
                rows="3"
                placeholder="Optional description"
                class="flex w-full rounded-md border border-input bg-background px-3 py-2 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <p v-if="productError" class="text-sm text-destructive">{{ productError }}</p>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showProductModal = false"
              >
                Cancel
              </button>
              <button
                type="submit"
                :disabled="productLoading"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="productLoading">Saving…</span>
                <span v-else>{{ editingProduct ? 'Save' : 'Create' }}</span>
              </button>
            </div>
          </form>
        </div>
      </div>

      <!-- Category modal -->
      <div
        v-if="showCategoryModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showCategoryModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-4 text-lg font-semibold">Add category</h2>
          <form class="space-y-4" @submit.prevent="submitCategory">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="category-name">Name</label>
              <input
                id="category-name"
                v-model="categoryName"
                type="text"
                required
                placeholder="Category name"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <p v-if="categoryError" class="text-sm text-destructive">{{ categoryError }}</p>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showCategoryModal = false"
              >
                Cancel
              </button>
              <button
                type="submit"
                :disabled="categoryLoading"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="categoryLoading">Creating…</span>
                <span v-else>Create</span>
              </button>
            </div>
          </form>
        </div>
      </div>
    </SidebarInset>
  </SidebarProvider>
</template>
