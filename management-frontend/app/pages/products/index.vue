<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { formatCurrency } from '@/lib/utils'

const { organization, role } = useOrganization()
const { products, categories, loading, fetchProducts, createProduct, updateProduct, deleteProduct, uploadProductImage, deleteProductImage, createCategory, deleteCategory } = useProducts()
const { barcodes: allBarcodes, fetchBarcodes, addBarcode, removeBarcode } = useWarehouse()
const {
  products: importProducts,
  parsing: importParsing,
  importing: importRunning,
  parseError: importParseError,
  importResult,
  parseFile: importParseFile,
  executeImport: importExecute,
  toggleAll: importToggleAll,
  reset: importReset,
  selectedCount: importSelectedCount,
  allSelected: importAllSelected,
} = useImportProducts()

const isAdmin = computed(() => role.value === 'admin')

onMounted(async () => {
  await Promise.all([fetchProducts(), fetchBarcodes()])
})

// ── Barcode management (per-product in modal) ──────────────────────────────
const productBarcodes = computed(() => {
  if (!editingProduct.value) return []
  return allBarcodes.value.filter(b => b.product_id === editingProduct.value.id)
})
const newBarcodeInput = ref('')
const barcodeAddError = ref('')

async function addProductBarcode() {
  if (!newBarcodeInput.value.trim() || !editingProduct.value) return
  barcodeAddError.value = ''
  try {
    await addBarcode({ product_id: editingProduct.value.id, barcode: newBarcodeInput.value.trim() })
    newBarcodeInput.value = ''
  } catch (err: any) {
    barcodeAddError.value = err.message ?? 'Failed to add barcode'
  }
}

async function removeProductBarcode(id: string) {
  await removeBarcode(id)
}

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
  newBarcodeInput.value = ''
  barcodeAddError.value = ''
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

// ── Import modal ────────────────────────────────────────────────────────────
const showImportModal = ref(false)
const importStep = ref<1 | 2 | 3>(1)

function openImportModal() {
  importStep.value = 1
  importReset()
  showImportModal.value = true
}

function closeImportModal() {
  showImportModal.value = false
  if (importStep.value === 3 && importResult.value && importResult.value.created > 0) {
    fetchProducts()
  }
}

async function onImportFileSelected(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  if (!file) return
  await importParseFile(file)
  if (importProducts.value.length > 0) {
    importStep.value = 2
  }
}

async function onImportFileDrop(event: DragEvent) {
  event.preventDefault()
  const file = event.dataTransfer?.files?.[0]
  if (!file) return
  await importParseFile(file)
  if (importProducts.value.length > 0) {
    importStep.value = 2
  }
}

// Bulk price helpers
const bulkPrice = ref<number | null>(null)
const emptyPriceCount = computed(() => importProducts.value.filter(p => p.selected && !p.sellprice).length)

function applyBulkPrice() {
  if (!bulkPrice.value) return
  for (const p of importProducts.value) {
    if (p.selected && !p.sellprice) {
      p.sellprice = bulkPrice.value
    }
  }
}

async function runImport() {
  await importExecute()
  importStep.value = 3
}
</script>

<template>
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
              <div v-if="isAdmin" class="flex gap-2">
                <button
                  class="inline-flex h-9 items-center justify-center rounded-md border px-4 text-sm font-medium transition-colors hover:bg-muted"
                  @click="openImportModal"
                >
                  Import
                </button>
                <button
                  class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
                  @click="openAddProduct"
                >
                  Add product
                </button>
              </div>
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
            <!-- Barcodes (only when editing existing product) -->
            <div v-if="editingProduct && isAdmin" class="space-y-2">
              <label class="text-sm font-medium">Barcodes</label>
              <div v-if="productBarcodes.length > 0" class="flex flex-wrap gap-1.5">
                <span
                  v-for="b in productBarcodes"
                  :key="b.id"
                  class="inline-flex items-center gap-1 rounded-full border bg-muted/50 py-0.5 pl-2.5 pr-1 text-xs font-mono"
                >
                  {{ b.barcode }}
                  <button
                    type="button"
                    class="ml-0.5 flex size-4 items-center justify-center rounded-full text-muted-foreground hover:bg-destructive hover:text-destructive-foreground"
                    @click="removeProductBarcode(b.id)"
                  >
                    &times;
                  </button>
                </span>
              </div>
              <div class="flex gap-1.5">
                <input
                  v-model="newBarcodeInput"
                  type="text"
                  placeholder="EAN barcode..."
                  class="flex h-8 flex-1 rounded-md border border-input bg-background px-2 text-xs font-mono shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                  @keydown.enter.prevent="addProductBarcode"
                />
                <button
                  type="button"
                  class="h-8 rounded-md border px-2 text-xs font-medium hover:bg-muted"
                  @click="addProductBarcode"
                >
                  Add
                </button>
              </div>
              <p v-if="barcodeAddError" class="text-xs text-destructive">{{ barcodeAddError }}</p>
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

      <!-- Import modal -->
      <div
        v-if="showImportModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="closeImportModal"
      >
        <div class="w-full max-w-3xl rounded-xl border bg-card p-6 shadow-lg max-h-[80vh] flex flex-col">

          <!-- Step 1: File upload -->
          <template v-if="importStep === 1">
            <h2 class="mb-1 text-lg font-semibold">Import Products</h2>
            <p class="mb-5 text-sm text-muted-foreground">
              Upload a Nayax product export (.xlsx). Products, categories, and images will be imported automatically.
            </p>

            <label
              class="flex h-40 w-full cursor-pointer items-center justify-center rounded-lg border-2 border-dashed border-muted-foreground/25 text-muted-foreground transition-colors hover:border-primary/50 hover:bg-primary/5"
              @dragover.prevent
              @drop="onImportFileDrop"
            >
              <div class="text-center">
                <svg xmlns="http://www.w3.org/2000/svg" class="mx-auto mb-2 h-8 w-8" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" x2="12" y1="3" y2="15"/></svg>
                <span v-if="importParsing" class="text-sm">Parsing file…</span>
                <template v-else>
                  <span class="text-sm font-medium">Drop .xlsx file here or click to browse</span>
                  <span class="mt-1 block text-xs">Supports Nayax product exports</span>
                </template>
              </div>
              <input
                type="file"
                accept=".xlsx,.xls"
                class="hidden"
                @change="onImportFileSelected"
              />
            </label>

            <p v-if="importParseError" class="mt-3 text-sm text-destructive">{{ importParseError }}</p>

            <div class="mt-5 flex justify-end">
              <button
                class="inline-flex h-9 items-center justify-center rounded-md border px-4 text-sm font-medium hover:bg-muted"
                @click="closeImportModal"
              >
                Cancel
              </button>
            </div>
          </template>

          <!-- Step 2: Preview table -->
          <template v-else-if="importStep === 2">
            <div class="mb-4 flex items-center justify-between">
              <div>
                <h2 class="text-lg font-semibold">Review Products</h2>
                <p class="text-sm text-muted-foreground">
                  {{ importSelectedCount }} of {{ importProducts.length }} products selected
                  <template v-if="emptyPriceCount > 0">
                    · <span class="text-yellow-600">{{ emptyPriceCount }} without price</span>
                  </template>
                </p>
              </div>
              <div v-if="emptyPriceCount > 0" class="flex items-center gap-2">
                <input
                  v-model.number="bulkPrice"
                  type="number"
                  step="0.01"
                  min="0"
                  placeholder="Price"
                  class="h-8 w-24 rounded-md border border-input bg-background px-2 text-sm placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                />
                <button
                  class="inline-flex h-8 items-center justify-center rounded-md border px-3 text-xs font-medium hover:bg-muted"
                  :disabled="!bulkPrice"
                  @click="applyBulkPrice"
                >
                  Fill empty prices
                </button>
              </div>
            </div>

            <div class="flex-1 overflow-auto rounded-md border min-h-0">
              <table class="w-full text-sm">
                <thead class="sticky top-0 bg-card z-10">
                  <tr class="border-b bg-muted/50 text-left">
                    <th class="w-10 px-3 py-2">
                      <input
                        type="checkbox"
                        :checked="importAllSelected"
                        class="rounded"
                        @change="importToggleAll(($event.target as HTMLInputElement).checked)"
                      />
                    </th>
                    <th class="px-3 py-2 font-medium">Name</th>
                    <th class="px-3 py-2 font-medium">Category</th>
                    <th class="px-3 py-2 font-medium">Price</th>
                    <th class="w-16 px-3 py-2 font-medium text-center">Image</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="(p, i) in importProducts"
                    :key="i"
                    class="border-b last:border-0 transition-colors"
                    :class="p.selected ? 'hover:bg-muted/30' : 'opacity-40'"
                  >
                    <td class="px-3 py-2">
                      <input
                        v-model="p.selected"
                        type="checkbox"
                        class="rounded"
                      />
                    </td>
                    <td class="px-3 py-2 font-medium">{{ p.name }}</td>
                    <td class="px-3 py-2 text-muted-foreground">{{ p.category_name ?? '—' }}</td>
                    <td class="px-3 py-1">
                      <input
                        v-model.number="p.sellprice"
                        type="number"
                        step="0.01"
                        min="0"
                        placeholder="—"
                        class="h-7 w-20 rounded border border-input bg-background px-2 text-sm tabular-nums transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                        :class="!p.sellprice ? 'border-yellow-500/50' : ''"
                      />
                    </td>
                    <td class="px-3 py-2 text-center">
                      <span v-if="p.image_url" class="inline-flex h-5 w-5 items-center justify-center rounded-full bg-green-500/10 text-green-600">
                        <svg xmlns="http://www.w3.org/2000/svg" class="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>
                      </span>
                      <span v-else class="text-xs text-muted-foreground">—</span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>

            <div class="mt-4 flex gap-2">
              <button
                class="inline-flex h-9 items-center justify-center rounded-md border px-4 text-sm font-medium hover:bg-muted"
                @click="closeImportModal"
              >
                Cancel
              </button>
              <button
                class="inline-flex h-9 items-center justify-center rounded-md border px-4 text-sm font-medium hover:bg-muted"
                @click="importStep = 1; importReset()"
              >
                Back
              </button>
              <div class="flex-1" />
              <button
                :disabled="importSelectedCount === 0 || importRunning"
                class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-6 text-sm font-medium text-primary-foreground shadow hover:bg-primary/90 disabled:opacity-50"
                @click="runImport"
              >
                <template v-if="importRunning">
                  <svg class="mr-2 h-4 w-4 animate-spin" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24"><circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"/><path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"/></svg>
                  Importing…
                </template>
                <template v-else>
                  Import {{ importSelectedCount }} products
                </template>
              </button>
            </div>
          </template>

          <!-- Step 3: Results -->
          <template v-else>
            <h2 class="mb-4 text-lg font-semibold">Import Complete</h2>

            <div class="space-y-3">
              <div class="flex items-center gap-3 rounded-lg border p-4">
                <div class="flex h-10 w-10 shrink-0 items-center justify-center rounded-full bg-green-500/10">
                  <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5 text-green-600" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>
                </div>
                <div>
                  <p class="font-medium">{{ importResult?.created ?? 0 }} products created</p>
                  <p v-if="(importResult?.skipped ?? 0) > 0" class="text-sm text-muted-foreground">
                    {{ importResult?.skipped }} skipped (already exist)
                  </p>
                </div>
              </div>

              <div v-if="(importResult?.image_errors ?? 0) > 0" class="flex items-center gap-3 rounded-lg border border-yellow-500/30 p-4">
                <div class="flex h-10 w-10 shrink-0 items-center justify-center rounded-full bg-yellow-500/10">
                  <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5 text-yellow-600" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/><line x1="12" x2="12" y1="9" y2="13"/><line x1="12" x2="12.01" y1="17" y2="17"/></svg>
                </div>
                <div>
                  <p class="font-medium">{{ importResult?.image_errors }} image downloads failed</p>
                  <p class="text-sm text-muted-foreground">Products were created without images</p>
                </div>
              </div>

              <details v-if="importResult?.errors?.length" class="rounded-lg border p-4">
                <summary class="cursor-pointer text-sm font-medium text-muted-foreground">
                  Show {{ importResult.errors.length }} error details
                </summary>
                <ul class="mt-2 max-h-40 space-y-1 overflow-auto text-xs text-muted-foreground">
                  <li v-for="(err, i) in importResult.errors" :key="i">{{ err }}</li>
                </ul>
              </details>
            </div>

            <div class="mt-5 flex justify-end">
              <button
                class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow hover:bg-primary/90"
                @click="closeImportModal"
              >
                Done
              </button>
            </div>
          </template>
        </div>
      </div>
</template>
