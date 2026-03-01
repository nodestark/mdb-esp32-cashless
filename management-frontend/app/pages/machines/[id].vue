<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { VisArea, VisAxis, VisLine, VisXYContainer } from '@unovis/vue'

const route = useRoute()
const supabase = useSupabaseClient()
const { role } = useOrganization()
const { products, categories, fetchProducts } = useProducts()
const { trays, loading: traysLoading, fetchTrays, upsertTray, updateTray, batchCreateTrays, refillTray, deleteTray, subscribeToTrayUpdates } = useMachineTrays()
const { fetchUnassignedEmbeddeds, swapDevice } = useMachines()

const isAdmin = computed(() => role.value === 'admin')

const machine = ref<any>(null)
const sales = ref<any[]>([])
const loading = ref(true)
const errorMsg = ref('')

onMounted(async () => {
  const id = route.params.id as string
  try {
    const { data: machineData, error: machineError } = await supabase
      .from('vendingMachine')
      .select('id, name, location_lat, location_lon, embedded, embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date)')
      .eq('id', id)
      .single()

    if (machineError) throw machineError
    machine.value = machineData

    // Fetch trays, products, and sales in parallel — sales query uses machine_id (not embedded_id)
    const thirtyDaysAgo = new Date(Date.now() - 30 * 24 * 60 * 60 * 1000).toISOString()
    const promises: PromiseLike<any>[] = [
      fetchTrays(id),
      fetchProducts(),
      supabase
        .from('sales')
        .select('created_at, item_price, item_number, channel')
        .eq('machine_id', id)
        .gte('created_at', thirtyDaysAgo)
        .order('created_at', { ascending: false })
        .then(({ data: salesData, error: salesError }: any) => {
          if (salesError) throw salesError
          sales.value = salesData ?? []
        }),
    ]

    await Promise.all(promises)

    // Subscribe to live sales updates (by machine_id — works regardless of current device)
    const salesChannel = supabase
      .channel(`machine-sales-${id}`)
      .on(
        'postgres_changes',
        {
          event: 'INSERT',
          schema: 'public',
          table: 'sales',
          filter: `machine_id=eq.${id}`,
        },
        (payload) => {
          const newSale = payload.new as Record<string, any>
          sales.value.unshift(newSale)
        }
      )
      .subscribe()

    onUnmounted(() => supabase.removeChannel(salesChannel))

    // Subscribe to embedded status updates (only if a device is assigned)
    if (machineData.embeddeds?.id) {
      const statusChannel = supabase
        .channel(`machine-status-${machineData.embeddeds.id}`)
        .on(
          'postgres_changes',
          {
            event: 'UPDATE',
            schema: 'public',
            table: 'embeddeds',
            filter: `id=eq.${machineData.embeddeds.id}`,
          },
          (payload) => {
            if (machine.value?.embeddeds) {
              machine.value.embeddeds.status = payload.new.status
              machine.value.embeddeds.status_at = payload.new.status_at
              if (payload.new.firmware_version) {
                machine.value.embeddeds.firmware_version = payload.new.firmware_version
              }
              if (payload.new.firmware_build_date) {
                machine.value.embeddeds.firmware_build_date = payload.new.firmware_build_date
              }
            }
          }
        )
        .subscribe()

      onUnmounted(() => supabase.removeChannel(statusChannel))
    }

    // Subscribe to tray realtime updates
    const unsubTrays = subscribeToTrayUpdates(id)
    onUnmounted(unsubTrays)
  } catch (err: unknown) {
    errorMsg.value = err instanceof Error ? err.message : 'Failed to load machine'
  } finally {
    loading.value = false
  }
})

// Aggregate sales per day for the chart
const salesChartData = computed(() => {
  const byDay: Record<string, number> = {}
  for (const sale of sales.value) {
    const day = new Date(sale.created_at).toISOString().slice(0, 10)
    byDay[day] = (byDay[day] ?? 0) + (sale.item_price ?? 0)
  }
  return Object.entries(byDay)
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([date, total]) => ({ date: new Date(date), total }))
})

type ChartPoint = { date: Date; total: number }

function formatDate(dt: string | null | undefined) {
  if (!dt) return '—'
  return new Date(dt).toLocaleString()
}

function formatCurrency(amount: number | null | undefined) {
  if (amount == null) return '—'
  return new Intl.NumberFormat('en-US', { style: 'currency', currency: 'EUR' }).format(amount)
}

function timeAgo(dt: string) {
  const diff = Date.now() - new Date(dt).getTime()
  const mins = Math.floor(diff / 60000)
  if (mins < 1) return 'just now'
  if (mins < 60) return `${mins}m ago`
  const hrs = Math.floor(mins / 60)
  if (hrs < 24) return `${hrs}h ago`
  const days = Math.floor(hrs / 24)
  return `${days}d ago`
}

// Map item_number → product info from trays
const trayProductMap = computed(() => {
  const map = new Map<number, { name: string; image_url: string | null }>()
  for (const t of trays.value) {
    if (t.product_name) {
      const product = products.value.find(p => p.id === t.product_id)
      map.set(t.item_number, {
        name: t.product_name,
        image_url: product?.image_url ?? null,
      })
    }
  }
  return map
})

// ── Inline name editing ─────────────────────────────────────────────────────
const editingName = ref(false)
const editName = ref('')
const savingName = ref(false)

function startEditName() {
  editName.value = machine.value?.name ?? ''
  editingName.value = true
  nextTick(() => {
    const input = document.getElementById('machine-name-input') as HTMLInputElement | null
    input?.focus()
    input?.select()
  })
}

function cancelEditName() {
  editingName.value = false
}

async function saveNameEdit() {
  const trimmed = editName.value.trim()
  if (!trimmed || trimmed === machine.value?.name) {
    editingName.value = false
    return
  }
  savingName.value = true
  try {
    const { error } = await supabase
      .from('vendingMachine')
      .update({ name: trimmed })
      .eq('id', machine.value.id)
    if (error) throw error
    machine.value.name = trimmed
  } catch (err: unknown) {
    // Revert on failure — no UI noise, just keep old name
  } finally {
    savingName.value = false
    editingName.value = false
  }
}

// ── Device swap ─────────────────────────────────────────────────────────────
const showDeviceModal = ref(false)
const availableDevices = ref<any[]>([])
const selectedDeviceId = ref('')
const deviceSwapLoading = ref(false)
const deviceSwapError = ref('')

async function openDeviceModal() {
  deviceSwapError.value = ''
  selectedDeviceId.value = ''
  deviceSwapLoading.value = false
  showDeviceModal.value = true
  try {
    availableDevices.value = await fetchUnassignedEmbeddeds()
  } catch {
    deviceSwapError.value = 'Failed to load available devices'
  }
}

async function submitDeviceSwap() {
  if (!selectedDeviceId.value) return
  deviceSwapLoading.value = true
  deviceSwapError.value = ''
  try {
    await swapDevice(machine.value.id, selectedDeviceId.value)
    // Re-fetch machine to get updated embeddeds join
    const { data } = await supabase
      .from('vendingMachine')
      .select('id, name, location_lat, location_lon, embedded, embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date)')
      .eq('id', machine.value.id)
      .single()
    if (data) machine.value = data
    showDeviceModal.value = false
  } catch (err: unknown) {
    deviceSwapError.value = err instanceof Error ? err.message : 'Failed to swap device'
  } finally {
    deviceSwapLoading.value = false
  }
}

async function detachDevice() {
  deviceSwapLoading.value = true
  try {
    await swapDevice(machine.value.id, null)
    const { data } = await supabase
      .from('vendingMachine')
      .select('id, name, location_lat, location_lon, embedded, embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date)')
      .eq('id', machine.value.id)
      .single()
    if (data) machine.value = data
  } catch (err: unknown) {
    // silent
  } finally {
    deviceSwapLoading.value = false
  }
}

// ── Tray management ─────────────────────────────────────────────────────────
const showTrayModal = ref(false)
const trayForm = ref({ item_number: 0, product_id: '', capacity: 10, current_stock: 0 })
const trayLoading = ref(false)
const trayError = ref('')

function openAddTray() {
  const maxSlot = trays.value.length > 0
    ? Math.max(...trays.value.map(t => t.item_number)) + 1
    : 0
  trayForm.value = { item_number: maxSlot, product_id: '', capacity: 10, current_stock: 0 }
  trayError.value = ''
  showTrayModal.value = true
}

async function submitTray() {
  if (trayForm.value.item_number < 0) {
    trayError.value = 'Slot number must be 0 or greater'
    return
  }
  if (trayForm.value.capacity < 1) {
    trayError.value = 'Capacity must be at least 1'
    return
  }
  if (trayForm.value.current_stock > trayForm.value.capacity) {
    trayError.value = 'Stock cannot exceed capacity'
    return
  }
  trayLoading.value = true
  trayError.value = ''
  try {
    await upsertTray({
      machine_id: machine.value.id,
      item_number: trayForm.value.item_number,
      product_id: trayForm.value.product_id || null,
      capacity: trayForm.value.capacity,
      current_stock: trayForm.value.current_stock,
    })
    showTrayModal.value = false
  } catch (err: unknown) {
    trayError.value = err instanceof Error ? err.message : 'Failed to save tray'
  } finally {
    trayLoading.value = false
  }
}

// ── Inline tray editing ─────────────────────────────────────────────────────
const activeAutocompleteTrayId = ref<string | null>(null)
const productQuery = ref('')
const highlightedIndex = ref(-1)

const filteredProducts = computed(() => {
  const q = productQuery.value.toLowerCase().trim()
  if (!q) return products.value
  return products.value.filter(p => p.name.toLowerCase().includes(q))
})

// Auto-highlight best match as the user types
watch(productQuery, () => {
  if (!activeAutocompleteTrayId.value) return
  const q = productQuery.value.toLowerCase().trim()
  if (q && filteredProducts.value.length > 0) {
    // Prefer "starts with" match; fall back to first contains match
    const startsIdx = filteredProducts.value.findIndex(p => p.name.toLowerCase().startsWith(q))
    highlightedIndex.value = startsIdx >= 0 ? startsIdx + 1 : 1 // +1 because 0 = "None"
  } else {
    highlightedIndex.value = -1
  }
})

function openProductAutocomplete(tray: any) {
  activeAutocompleteTrayId.value = tray.id
  productQuery.value = ''
  highlightedIndex.value = -1
  nextTick(() => {
    const input = document.getElementById(`product-input-${tray.id}`) as HTMLInputElement | null
    input?.focus()
  })
}

async function selectProduct(trayId: string, productId: string | null) {
  activeAutocompleteTrayId.value = null
  // Focus stock input immediately so keyboard flow continues
  nextTick(() => {
    const el = document.getElementById(`stock-${trayId}`) as HTMLInputElement | null
    el?.focus()
    el?.select()
  })
  try {
    await updateTray(trayId, machine.value.id, { product_id: productId })
  } catch {
    // silent — tray reverts to previous value via fetchTrays
  }
}

function handleProductBlur(trayId: string) {
  // Delay to allow click on dropdown item to register first
  setTimeout(() => {
    if (activeAutocompleteTrayId.value === trayId) {
      activeAutocompleteTrayId.value = null
    }
  }, 200)
}

function handleProductKeydown(event: KeyboardEvent, trayId: string) {
  const itemCount = filteredProducts.value.length + 1 // +1 for "None" option
  if (event.key === 'Escape') {
    event.preventDefault()
    activeAutocompleteTrayId.value = null
    // Return focus to the product button
    nextTick(() => {
      const btn = document.getElementById(`product-btn-${trayId}`) as HTMLElement | null
      btn?.focus()
    })
    return
  }
  if (event.key === 'ArrowDown') {
    event.preventDefault()
    highlightedIndex.value = Math.min(highlightedIndex.value + 1, itemCount - 1)
    return
  }
  if (event.key === 'ArrowUp') {
    event.preventDefault()
    highlightedIndex.value = Math.max(highlightedIndex.value - 1, 0)
    return
  }
  if (event.key === 'Enter') {
    event.preventDefault()
    if (highlightedIndex.value === 0) {
      selectProduct(trayId, null)
    } else if (highlightedIndex.value > 0 && highlightedIndex.value <= filteredProducts.value.length) {
      selectProduct(trayId, filteredProducts.value[highlightedIndex.value - 1].id)
    }
    return
  }
  if (event.key === 'Tab' && !event.shiftKey) {
    event.preventDefault()
    // Auto-select highlighted product and advance to stock
    if (highlightedIndex.value === 0) {
      selectProduct(trayId, null)
    } else if (highlightedIndex.value > 0 && highlightedIndex.value <= filteredProducts.value.length) {
      selectProduct(trayId, filteredProducts.value[highlightedIndex.value - 1].id)
    } else {
      // Nothing highlighted — just close and advance to stock
      activeAutocompleteTrayId.value = null
      nextTick(() => {
        const el = document.getElementById(`stock-${trayId}`) as HTMLInputElement | null
        el?.focus()
        el?.select()
      })
    }
    return
  }
  // Shift+Tab: let default behaviour move focus backwards; blur handler closes the dropdown
}

async function saveInlineField(trayId: string, field: 'capacity' | 'current_stock', value: number) {
  const tray = trays.value.find(t => t.id === trayId)
  if (!tray) return

  // Validate
  if (field === 'capacity' && value < 1) return
  if (field === 'current_stock' && (value < 0 || value > (tray.capacity ?? 100))) return

  // Skip save if value unchanged
  if (tray[field] === value) return

  try {
    await updateTray(trayId, machine.value.id, { [field]: value })
  } catch {
    // silent — reverts via fetchTrays
  }
}

function handleStockKeydown(event: KeyboardEvent, trayId: string) {
  if (event.key === 'Enter') {
    event.preventDefault()
    const val = parseInt((event.target as HTMLInputElement).value) || 0
    saveInlineField(trayId, 'current_stock', val)
    // Advance to capacity input
    nextTick(() => {
      const el = document.getElementById(`capacity-${trayId}`) as HTMLInputElement | null
      el?.focus()
      el?.select()
    })
  }
  if (event.key === 'Escape') {
    (event.target as HTMLInputElement).blur()
  }
}

function handleCapacityKeydown(event: KeyboardEvent, trayId: string) {
  if (event.key === 'Enter') {
    event.preventDefault()
    const val = parseInt((event.target as HTMLInputElement).value) || 1
    saveInlineField(trayId, 'capacity', val)
    // Advance to next row's product button (or blur if last row)
    const idx = trays.value.findIndex(t => t.id === trayId)
    const nextTray = trays.value[idx + 1]
    if (nextTray) {
      nextTick(() => {
        const el = document.getElementById(`product-btn-${nextTray.id}`) as HTMLElement | null
        el?.focus()
      })
    } else {
      (event.target as HTMLInputElement).blur()
    }
  }
  if (event.key === 'Escape') {
    (event.target as HTMLInputElement).blur()
  }
}

// Batch add trays
const showBatchModal = ref(false)
const batchForm = ref({ startSlot: 0, count: 10, capacity: 10 })
const batchLoading = ref(false)
const batchError = ref('')

function openBatchAdd() {
  const maxSlot = trays.value.length > 0
    ? Math.max(...trays.value.map(t => t.item_number)) + 1
    : 0
  batchForm.value = { startSlot: maxSlot, count: 10, capacity: 10 }
  batchError.value = ''
  showBatchModal.value = true
}

async function submitBatch() {
  if (batchForm.value.count < 1) {
    batchError.value = 'Count must be at least 1'
    return
  }
  if (batchForm.value.count > 100) {
    batchError.value = 'Maximum 100 trays at once'
    return
  }
  if (batchForm.value.startSlot < 0) {
    batchError.value = 'Start slot must be 0 or greater'
    return
  }
  if (batchForm.value.capacity < 1) {
    batchError.value = 'Capacity must be at least 1'
    return
  }
  batchLoading.value = true
  batchError.value = ''
  try {
    await batchCreateTrays(machine.value.id, batchForm.value.startSlot, batchForm.value.count, batchForm.value.capacity)
    showBatchModal.value = false
  } catch (err: unknown) {
    batchError.value = err instanceof Error ? err.message : 'Failed to create trays'
  } finally {
    batchLoading.value = false
  }
}

// Refill modal
const showRefillModal = ref(false)
const refillTarget = ref<any>(null)
const refillStock = ref(0)
const refillLoading = ref(false)

function openRefill(tray: any) {
  refillTarget.value = tray
  refillStock.value = tray.current_stock
  showRefillModal.value = true
}

async function submitRefill() {
  if (!refillTarget.value) return
  refillLoading.value = true
  try {
    await refillTray(refillTarget.value.id, machine.value.id, refillStock.value)
    showRefillModal.value = false
  } catch (err: unknown) {
    // silent
  } finally {
    refillLoading.value = false
  }
}

async function handleDeleteTray(trayId: string) {
  try {
    await deleteTray(trayId, machine.value.id)
  } catch (err: unknown) {
    // silent
  }
}

function stockPercent(tray: any) {
  if (tray.capacity === 0) return 0
  return Math.round((tray.current_stock / tray.capacity) * 100)
}

function stockColor(percent: number) {
  if (percent > 50) return 'bg-green-500'
  if (percent > 20) return 'bg-yellow-500'
  return 'bg-red-500'
}
</script>

<template>
  <div class="flex flex-1 flex-col gap-6 p-4 md:p-6">
        <div v-if="loading" class="text-muted-foreground">Loading…</div>
        <div v-else-if="errorMsg" class="text-destructive">{{ errorMsg }}</div>
        <template v-else-if="machine">
          <!-- Machine info -->
          <div class="flex items-start justify-between">
            <div>
              <div v-if="editingName" class="flex items-center gap-2">
                <input
                  id="machine-name-input"
                  v-model="editName"
                  type="text"
                  class="h-9 rounded-md border bg-transparent px-3 text-2xl font-semibold shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                  @keydown.enter="saveNameEdit"
                  @keydown.escape="cancelEditName"
                  @blur="saveNameEdit"
                />
              </div>
              <h1
                v-else
                class="group flex cursor-pointer items-center gap-2 text-2xl font-semibold"
                @click="startEditName"
              >
                {{ machine.name ?? 'Unnamed machine' }}
                <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4 text-muted-foreground opacity-0 transition-opacity group-hover:opacity-100" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 3a2.85 2.83 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5Z"/><path d="m15 5 4 4"/></svg>
              </h1>
              <p v-if="machine.location_lat && machine.location_lon" class="mt-1 text-sm text-muted-foreground">
                {{ machine.location_lat.toFixed(5) }}, {{ machine.location_lon.toFixed(5) }}
              </p>
            </div>
            <!-- Device info + swap controls -->
            <div class="text-right">
              <template v-if="machine.embeddeds">
                <span
                  class="rounded-full px-3 py-1 text-xs font-medium"
                  :class="{
                    'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400': machine.embeddeds.status === 'online' || machine.embeddeds.status === 'ota_success',
                    'bg-yellow-100 text-yellow-700 dark:bg-yellow-900/30 dark:text-yellow-400': machine.embeddeds.status === 'ota_updating',
                    'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400': machine.embeddeds.status === 'ota_failed',
                    'bg-muted text-muted-foreground': !['online', 'ota_updating', 'ota_success', 'ota_failed'].includes(machine.embeddeds.status),
                  }"
                >
                  {{ machine.embeddeds.status === 'ota_updating' ? 'updating' : machine.embeddeds.status === 'ota_success' ? 'updated' : machine.embeddeds.status === 'ota_failed' ? 'update failed' : machine.embeddeds.status }}
                </span>
                <p class="mt-1 text-xs text-muted-foreground">
                  {{ machine.embeddeds.mac_address ?? `Subdomain ${machine.embeddeds.subdomain}` }}
                  <template v-if="machine.embeddeds.firmware_version">
                    <span class="ml-1 font-mono">v{{ machine.embeddeds.firmware_version }}</span>
                    <span v-if="machine.embeddeds.firmware_build_date" class="ml-1">(built {{ new Date(machine.embeddeds.firmware_build_date).toLocaleString() }})</span>
                  </template>
                </p>
                <p class="text-xs text-muted-foreground">Since {{ formatDate(machine.embeddeds.status_at) }}</p>
                <div v-if="isAdmin" class="mt-2 flex justify-end gap-2">
                  <button
                    class="text-xs text-primary hover:underline"
                    @click="openDeviceModal"
                  >
                    Change device
                  </button>
                  <button
                    class="text-xs text-destructive hover:underline"
                    :disabled="deviceSwapLoading"
                    @click="detachDevice"
                  >
                    Detach
                  </button>
                </div>
              </template>
              <template v-else>
                <span class="rounded-full bg-muted px-3 py-1 text-xs font-medium text-muted-foreground">
                  No device
                </span>
                <div v-if="isAdmin" class="mt-2">
                  <button
                    class="text-xs text-primary hover:underline"
                    @click="openDeviceModal"
                  >
                    Assign device
                  </button>
                </div>
              </template>
            </div>
          </div>

          <!-- Tabs: Sales | Trays & Stock -->
          <Tabs default-value="sales">
            <TabsList>
              <TabsTrigger value="sales">Sales</TabsTrigger>
              <TabsTrigger value="trays">Trays & Stock</TabsTrigger>
            </TabsList>

            <!-- Sales tab -->
            <TabsContent value="sales" class="mt-4 space-y-6">
              <!-- Sales chart -->
              <div v-if="salesChartData.length > 0" class="rounded-xl border bg-card p-6">
                <h2 class="mb-4 text-sm font-medium">Daily revenue (last 30 days)</h2>
                <VisXYContainer :data="salesChartData" class="h-48 w-full">
                  <VisArea :x="(d: ChartPoint) => d.date" :y="(d: ChartPoint) => d.total" color="var(--primary)" :opacity="0.3" />
                  <VisLine :x="(d: ChartPoint) => d.date" :y="(d: ChartPoint) => d.total" color="var(--primary)" :line-width="2" />
                  <VisAxis
                    type="x"
                    :x="(d: ChartPoint) => d.date"
                    :tick-format="(d: number) => new Date(d).toLocaleDateString('en-US', { month: 'short', day: 'numeric' })"
                    :num-ticks="6"
                    :tick-line="false"
                    :domain-line="false"
                    :grid-line="false"
                  />
                  <VisAxis type="y" :num-ticks="3" :tick-line="false" :domain-line="false" />
                </VisXYContainer>
              </div>

              <!-- Sales list -->
              <div>
                <h2 class="mb-3 text-lg font-medium">Sales history</h2>
                <div v-if="sales.length === 0" class="text-sm text-muted-foreground">No sales recorded in the last 30 days.</div>
                <div v-else class="space-y-2">
                  <div
                    v-for="sale in sales"
                    :key="sale.id"
                    class="group flex items-center gap-4 rounded-lg border bg-card p-4 transition-colors hover:bg-muted/40"
                  >
                    <!-- Product image or amount badge -->
                    <img
                      v-if="trayProductMap.get(sale.item_number)?.image_url"
                      :src="trayProductMap.get(sale.item_number)!.image_url!"
                      :alt="trayProductMap.get(sale.item_number)!.name"
                      class="h-11 w-11 shrink-0 rounded-full object-cover"
                    />
                    <div v-else class="flex h-11 w-11 shrink-0 items-center justify-center rounded-full bg-primary/10 text-sm font-semibold text-primary">
                      {{ formatCurrency(sale.item_price) }}
                    </div>
                    <!-- Main info -->
                    <div class="flex-1 min-w-0">
                      <div class="flex items-center gap-2">
                        <span class="font-medium truncate">
                          {{ trayProductMap.get(sale.item_number)?.name ?? `Item #${sale.item_number}` }}
                        </span>
                        <span
                          class="shrink-0 rounded-full px-2 py-0.5 text-[10px] font-medium uppercase tracking-wide"
                          :class="sale.channel === 'cashless'
                            ? 'bg-blue-500/10 text-blue-600 dark:text-blue-400'
                            : 'bg-emerald-500/10 text-emerald-600 dark:text-emerald-400'"
                        >
                          {{ sale.channel }}
                        </span>
                      </div>
                      <p class="mt-0.5 text-xs text-muted-foreground">
                        Slot {{ sale.item_number }}
                      </p>
                    </div>
                    <!-- Timestamp -->
                    <div class="shrink-0 text-right">
                      <span class="text-sm text-muted-foreground">{{ timeAgo(sale.created_at) }}</span>
                      <p class="mt-0.5 text-[11px] text-muted-foreground/60">{{ formatDate(sale.created_at) }}</p>
                    </div>
                  </div>
                </div>
              </div>
            </TabsContent>

            <!-- Trays & Stock tab -->
            <TabsContent value="trays" class="mt-4">
              <div class="mb-4 flex items-center justify-between">
                <h2 class="text-base font-medium">Tray Configuration</h2>
                <div v-if="isAdmin" class="flex items-center gap-2">
                  <button
                    class="inline-flex h-9 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                    @click="openBatchAdd"
                  >
                    Batch add
                  </button>
                  <button
                    class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
                    @click="openAddTray"
                  >
                    Add tray
                  </button>
                </div>
              </div>

              <div v-if="traysLoading" class="text-sm text-muted-foreground">Loading trays…</div>
              <div v-else-if="trays.length === 0" class="text-sm text-muted-foreground">No trays configured. Add trays to track stock levels.</div>
              <div v-else class="rounded-md border overflow-visible">
                <table class="w-full text-sm">
                  <thead>
                    <tr class="border-b bg-muted/50 text-left">
                      <th class="w-20 px-4 py-3 font-medium">Slot #</th>
                      <th class="px-4 py-3 font-medium">Product</th>
                      <th class="w-36 px-4 py-3 font-medium">Stock</th>
                      <th class="w-32 px-4 py-3 font-medium">Level</th>
                      <th v-if="isAdmin" class="w-24 px-4 py-3 font-medium">Actions</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr
                      v-for="tray in trays"
                      :key="tray.id"
                      class="border-b last:border-0 hover:bg-muted/30 transition-colors"
                    >
                      <!-- Slot # (read-only) -->
                      <td class="px-4 py-2 font-mono">{{ tray.item_number }}</td>

                      <!-- Product (inline autocomplete for admins) -->
                      <td class="px-4 py-2 relative">
                        <template v-if="isAdmin">
                          <div v-if="activeAutocompleteTrayId === tray.id" class="relative">
                            <input
                              :id="`product-input-${tray.id}`"
                              v-model="productQuery"
                              type="text"
                              placeholder="Search products…"
                              role="combobox"
                              aria-expanded="true"
                              aria-autocomplete="list"
                              autocomplete="off"
                              class="h-8 w-full rounded-md border border-input bg-background px-2 text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                              @blur="handleProductBlur(tray.id)"
                              @keydown="(e: KeyboardEvent) => handleProductKeydown(e, tray.id)"
                            />
                            <div class="absolute left-0 top-full z-50 mt-1 max-h-48 w-full min-w-[200px] overflow-auto rounded-md border bg-popover shadow-md" role="listbox">
                              <button
                                type="button"
                                tabindex="-1"
                                class="w-full px-3 py-1.5 text-left text-sm hover:bg-accent"
                                :class="{ 'bg-accent': highlightedIndex === 0 }"
                                role="option"
                                @mousedown.prevent="selectProduct(tray.id, null)"
                              >
                                <span class="text-muted-foreground italic">None</span>
                              </button>
                              <button
                                v-for="(p, idx) in filteredProducts"
                                :key="p.id"
                                type="button"
                                tabindex="-1"
                                class="w-full px-3 py-1.5 text-left text-sm hover:bg-accent"
                                :class="{ 'bg-accent': highlightedIndex === idx + 1 }"
                                role="option"
                                @mousedown.prevent="selectProduct(tray.id, p.id)"
                              >
                                {{ p.name }}
                              </button>
                              <div v-if="filteredProducts.length === 0 && productQuery.trim()" class="px-3 py-2 text-xs text-muted-foreground">
                                No products found
                              </div>
                            </div>
                          </div>
                          <button
                            v-else
                            :id="`product-btn-${tray.id}`"
                            type="button"
                            class="w-full text-left transition-colors hover:text-primary"
                            @click="openProductAutocomplete(tray)"
                            @keydown.enter.prevent="openProductAutocomplete(tray)"
                          >
                            {{ tray.product_name ?? '—' }}
                          </button>
                        </template>
                        <span v-else>{{ tray.product_name ?? '—' }}</span>
                      </td>

                      <!-- Stock (inline editable for admins) -->
                      <td class="px-4 py-2">
                        <template v-if="isAdmin">
                          <div class="flex items-center gap-1">
                            <input
                              :id="`stock-${tray.id}`"
                              type="number"
                              :value="tray.current_stock"
                              min="0"
                              :max="tray.capacity"
                              class="h-7 w-12 rounded border border-transparent bg-transparent px-1 text-center text-sm hover:border-input focus:border-input focus:bg-background focus:shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                              @change="(e: Event) => saveInlineField(tray.id, 'current_stock', parseInt((e.target as HTMLInputElement).value) || 0)"
                              @keydown="(e: KeyboardEvent) => handleStockKeydown(e, tray.id)"
                            />
                            <span class="text-muted-foreground">/</span>
                            <input
                              :id="`capacity-${tray.id}`"
                              type="number"
                              :value="tray.capacity"
                              min="1"
                              class="h-7 w-12 rounded border border-transparent bg-transparent px-1 text-center text-sm hover:border-input focus:border-input focus:bg-background focus:shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                              @change="(e: Event) => saveInlineField(tray.id, 'capacity', parseInt((e.target as HTMLInputElement).value) || 1)"
                              @keydown="(e: KeyboardEvent) => handleCapacityKeydown(e, tray.id)"
                            />
                          </div>
                        </template>
                        <span v-else class="text-muted-foreground">{{ tray.current_stock }} / {{ tray.capacity }}</span>
                      </td>

                      <!-- Level bar -->
                      <td class="px-4 py-2">
                        <div class="h-2 w-full rounded-full bg-muted">
                          <div
                            class="h-2 rounded-full transition-all"
                            :class="stockColor(stockPercent(tray))"
                            :style="{ width: `${stockPercent(tray)}%` }"
                          />
                        </div>
                      </td>

                      <!-- Actions (Refill + Remove only) -->
                      <td v-if="isAdmin" class="px-4 py-2">
                        <div class="flex items-center gap-2">
                          <button
                            class="text-xs text-primary hover:underline"
                            @click="openRefill(tray)"
                          >
                            Refill
                          </button>
                          <button
                            class="text-xs text-destructive hover:underline"
                            @click="handleDeleteTray(tray.id)"
                          >
                            Remove
                          </button>
                        </div>
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </TabsContent>
          </Tabs>
        </template>
      </div>

      <!-- Device swap/assign modal -->
      <div
        v-if="showDeviceModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showDeviceModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-4 text-lg font-semibold">{{ machine.embeddeds ? 'Change device' : 'Assign device' }}</h2>
          <p class="mb-4 text-sm text-muted-foreground">
            Select an available device to assign to this machine. The machine configuration (trays, products, sales history) will be preserved.
          </p>
          <form class="space-y-4" @submit.prevent="submitDeviceSwap">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="device-select">Available devices</label>
              <select
                id="device-select"
                v-model="selectedDeviceId"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              >
                <option value="" disabled>Select a device</option>
                <option v-for="d in availableDevices" :key="d.id" :value="d.id">
                  {{ d.mac_address ?? 'Unknown MAC' }} — subdomain {{ d.subdomain }} ({{ d.status }}{{ d.firmware_version ? `, v${d.firmware_version}` : '' }})
                </option>
              </select>
              <p v-if="availableDevices.length === 0" class="text-xs text-muted-foreground">No unassigned devices available. Provision a new device first.</p>
            </div>
            <p v-if="deviceSwapError" class="text-sm text-destructive">{{ deviceSwapError }}</p>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showDeviceModal = false"
              >
                Cancel
              </button>
              <button
                type="submit"
                :disabled="deviceSwapLoading || !selectedDeviceId"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="deviceSwapLoading">Assigning…</span>
                <span v-else>Assign</span>
              </button>
            </div>
          </form>
        </div>
      </div>

      <!-- Add Tray modal -->
      <div
        v-if="showTrayModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showTrayModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-4 text-lg font-semibold">Add tray</h2>
          <form class="space-y-4" @submit.prevent="submitTray">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="tray-slot">Slot number</label>
              <input
                id="tray-slot"
                v-model.number="trayForm.item_number"
                type="number"
                min="0"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="tray-product">Product</label>
              <select
                id="tray-product"
                v-model="trayForm.product_id"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              >
                <option value="">None</option>
                <option v-for="p in products" :key="p.id" :value="p.id">{{ p.name }}</option>
              </select>
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="tray-capacity">Capacity</label>
              <input
                id="tray-capacity"
                v-model.number="trayForm.capacity"
                type="number"
                min="1"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="tray-stock">Current stock</label>
              <input
                id="tray-stock"
                v-model.number="trayForm.current_stock"
                type="number"
                min="0"
                :max="trayForm.capacity"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <p v-if="trayError" class="text-sm text-destructive">{{ trayError }}</p>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showTrayModal = false"
              >
                Cancel
              </button>
              <button
                type="submit"
                :disabled="trayLoading"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="trayLoading">Creating…</span>
                <span v-else>Create</span>
              </button>
            </div>
          </form>
        </div>
      </div>

      <!-- Refill modal -->
      <div
        v-if="showRefillModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showRefillModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-4 text-lg font-semibold">Refill tray</h2>
          <p class="mb-4 text-sm text-muted-foreground">
            Slot #{{ refillTarget?.item_number }}
            <span v-if="refillTarget?.product_name"> — {{ refillTarget.product_name }}</span>
          </p>
          <form class="space-y-4" @submit.prevent="submitRefill">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="refill-stock">New stock level</label>
              <input
                id="refill-stock"
                v-model.number="refillStock"
                type="number"
                min="0"
                :max="refillTarget?.capacity ?? 100"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
              <p class="text-xs text-muted-foreground">Max capacity: {{ refillTarget?.capacity }}</p>
            </div>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showRefillModal = false"
              >
                Cancel
              </button>
              <button
                type="submit"
                :disabled="refillLoading"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="refillLoading">Updating…</span>
                <span v-else>Update stock</span>
              </button>
            </div>
          </form>
        </div>
      </div>
      <!-- Batch add trays modal -->
      <div
        v-if="showBatchModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showBatchModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-4 text-lg font-semibold">Batch add trays</h2>
          <p class="mb-4 text-sm text-muted-foreground">
            Create multiple trays with sequential slot numbers. Existing slots are skipped.
          </p>
          <form class="space-y-4" @submit.prevent="submitBatch">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="batch-start">Starting slot</label>
              <input
                id="batch-start"
                v-model.number="batchForm.startSlot"
                type="number"
                min="0"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="batch-count">Number of trays</label>
              <input
                id="batch-count"
                v-model.number="batchForm.count"
                type="number"
                min="1"
                max="100"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="batch-capacity">Capacity per tray</label>
              <input
                id="batch-capacity"
                v-model.number="batchForm.capacity"
                type="number"
                min="1"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <p class="text-xs text-muted-foreground">
              This will create slots {{ batchForm.startSlot }} – {{ batchForm.startSlot + batchForm.count - 1 }}
            </p>
            <p v-if="batchError" class="text-sm text-destructive">{{ batchError }}</p>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showBatchModal = false"
              >
                Cancel
              </button>
              <button
                type="submit"
                :disabled="batchLoading"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="batchLoading">Creating…</span>
                <span v-else>Create {{ batchForm.count }} trays</span>
              </button>
            </div>
          </form>
        </div>
      </div>
</template>
