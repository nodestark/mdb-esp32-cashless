<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from '@/components/ui/tooltip'
import { VisArea, VisAxis, VisLine, VisXYContainer } from '@unovis/vue'
import { IconCreditCard, IconCoins, IconSend } from '@tabler/icons-vue'
import { timeAgo, formatCurrency } from '@/lib/utils'

const route = useRoute()
const defaultTab = computed(() => {
  const tab = route.query.tab as string
  if (tab === 'stock') return 'trays'
  if (tab === 'mdb') return 'mdb'
  return 'sales'
})
const supabase = useSupabaseClient()
const { role } = useOrganization()
const { products, categories, fetchProducts } = useProducts()
const { trays, loading: traysLoading, fetchTrays, upsertTray, updateTray, batchCreateTrays, refillToFull, refillAll, deleteTray, subscribeToTrayUpdates } = useMachineTrays()
const { fetchUnassignedEmbeddeds, swapDevice } = useMachines()
const { logs: mdbLogs, loading: mdbLogsLoading, hasMore: mdbHasMore, fetchLogs: fetchMdbLogs, fetchMore: fetchMoreMdbLogs, subscribe: subscribeMdbLog, stateLabel, stateVariant } = useMdbLog()
const { onResume } = useAppResume()

const isAdmin = computed(() => role.value === 'admin')

const machine = ref<any>(null)
const sales = ref<any[]>([])
const loading = ref(true)
const errorMsg = ref('')

// Re-fetch machine data when app resumes from background (iOS PWA etc.)
onResume(async () => {
  const id = route.params.id as string
  const [machineRes] = await Promise.all([
    supabase.from('vendingMachine')
      .select('id, name, location_lat, location_lon, embedded, embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date, mdb_address, mdb_diagnostics)')
      .eq('id', id).single(),
    fetchTrays(id),
  ])
  if (machineRes.data) machine.value = machineRes.data
})

onMounted(async () => {
  const id = route.params.id as string
  try {
    const { data: machineData, error: machineError } = await supabase
      .from('vendingMachine')
      .select('id, name, location_lat, location_lon, embedded, embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date, mdb_address, mdb_diagnostics)')
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
              if (payload.new.mdb_address !== undefined) {
                machine.value.embeddeds.mdb_address = payload.new.mdb_address
              }
              if (payload.new.mdb_diagnostics !== undefined) {
                machine.value.embeddeds.mdb_diagnostics = payload.new.mdb_diagnostics
              }
            }
          }
        )
        .subscribe()

      onUnmounted(() => supabase.removeChannel(statusChannel))

      // Fetch MDB log history + subscribe to live updates
      fetchMdbLogs(machineData.embeddeds.id)
      const unsubMdbLog = subscribeMdbLog(machineData.embeddeds.id)
      onUnmounted(unsubMdbLog)
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

// Map item_number → product info from trays
const trayProductMap = computed(() => {
  const map = new Map<number, { name: string; image_url: string | null; sellprice: number | null }>()
  for (const t of trays.value) {
    if (t.product_name) {
      const product = products.value.find(p => p.id === t.product_id)
      map.set(t.item_number, {
        name: t.product_name,
        image_url: product?.image_url ?? null,
        sellprice: product?.sellprice ?? null,
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

// ── Device info modal ────────────────────────────────────────────────────────
const showDeviceInfoModal = ref(false)

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
      .select('id, name, location_lat, location_lon, embedded, embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date, mdb_address, mdb_diagnostics)')
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
      .select('id, name, location_lat, location_lon, embedded, embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date, mdb_address, mdb_diagnostics)')
      .eq('id', machine.value.id)
      .single()
    if (data) machine.value = data
  } catch (err: unknown) {
    // silent
  } finally {
    deviceSwapLoading.value = false
  }
}

// ── Send credit ─────────────────────────────────────────────────────────────
const showCreditModal = ref(false)
const creditAmount = ref('')
const creditLoading = ref(false)
const creditError = ref('')
const creditSuccess = ref('')

function openCreditModal() {
  creditAmount.value = ''
  creditError.value = ''
  creditSuccess.value = ''
  creditLoading.value = false
  showCreditModal.value = true
}

async function submitCredit() {
  const amount = parseFloat(creditAmount.value)
  if (!amount || amount <= 0) {
    creditError.value = 'Enter a valid amount'
    return
  }
  creditLoading.value = true
  creditError.value = ''
  creditSuccess.value = ''
  try {
    const session = useSupabaseSession()
    const token = session.value?.access_token
    if (!token) throw new Error('Not authenticated')

    const result = await $fetch<{ status: string }>('/functions/v1/send-credit', {
      baseURL: useRuntimeConfig().public.supabase.url,
      method: 'POST',
      headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
      body: { device_id: machine.value.embeddeds.id, amount },
    })
    if (result?.status === 'online') {
      creditSuccess.value = `Credit of ${formatCurrency(amount)} sent successfully`
    } else {
      creditSuccess.value = `Credit queued (device is ${result?.status ?? 'unknown'})`
    }
    creditAmount.value = ''
  } catch (err: unknown) {
    const fetchErr = err as any
    creditError.value = fetchErr?.data?.error ?? fetchErr?.data?.message ?? fetchErr?.message ?? 'Failed to send credit'
  } finally {
    creditLoading.value = false
  }
}

const cancelCreditLoading = ref(false)

async function cancelCredit() {
  cancelCreditLoading.value = true
  creditError.value = ''
  creditSuccess.value = ''
  try {
    const session = useSupabaseSession()
    const token = session.value?.access_token
    if (!token) throw new Error('Not authenticated')

    await $fetch<{ status: string }>('/functions/v1/send-credit', {
      baseURL: useRuntimeConfig().public.supabase.url,
      method: 'POST',
      headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
      body: { device_id: machine.value.embeddeds.id, amount: 0 },
    })
    creditSuccess.value = 'Credit cancelled'
  } catch (err: unknown) {
    const fetchErr = err as any
    creditError.value = fetchErr?.data?.error ?? fetchErr?.data?.message ?? fetchErr?.message ?? 'Failed to cancel credit'
  } finally {
    cancelCreditLoading.value = false
  }
}

// ── MDB Address config ──────────────────────────────────────────────────────
const mdbAddressLoading = ref(false)
const mdbAddressError = ref('')

async function setMdbAddress(address: 1 | 2) {
  if (!machine.value?.embeddeds?.id) return
  if ((machine.value.embeddeds as any).mdb_address === address) return

  mdbAddressLoading.value = true
  mdbAddressError.value = ''
  try {
    const session = useSupabaseSession()
    const token = session.value?.access_token
    if (!token) throw new Error('Not authenticated')

    const { data, error } = await useFetch('/functions/v1/send-device-config', {
      baseURL: useRuntimeConfig().public.supabase.url,
      method: 'POST',
      headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
      body: { device_id: machine.value.embeddeds.id, config: { mdb_address: address } },
    })

    if (error.value) throw new Error((error.value as any).data?.error ?? error.value.message ?? 'Failed to update config')

    // Optimistically update the local state
    ;(machine.value.embeddeds as any).mdb_address = address
  } catch (err: unknown) {
    mdbAddressError.value = err instanceof Error ? err.message : 'Failed to update MDB address'
  } finally {
    mdbAddressLoading.value = false
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

async function saveInlineField(trayId: string, field: 'capacity' | 'current_stock' | 'min_stock' | 'fill_when_below', value: number) {
  const tray = trays.value.find(t => t.id === trayId)
  if (!tray) return

  // Validate
  if (field === 'capacity' && value < 1) return
  if (field === 'current_stock' && (value < 0 || value > (tray.capacity ?? 100))) return
  if (field === 'min_stock' && value < 0) return
  if (field === 'fill_when_below' && (value < 0 || (value > 0 && value < (tray.min_stock || 0)))) return

  // Skip save if value unchanged
  if (tray[field] === value) return

  // If min_stock is raised above fill_when_below, bump fill_when_below up too
  if (field === 'min_stock' && tray.fill_when_below > 0 && value > tray.fill_when_below) {
    try {
      await updateTray(trayId, machine.value.id, { min_stock: value, fill_when_below: value })
    } catch { /* silent */ }
    return
  }

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
    // Advance to min-stock input
    nextTick(() => {
      const el = document.getElementById(`min-stock-${trayId}`) as HTMLInputElement | null
      el?.focus()
      el?.select()
    })
  }
  if (event.key === 'Escape') {
    (event.target as HTMLInputElement).blur()
  }
}

function handleMinStockKeydown(event: KeyboardEvent, trayId: string) {
  if (event.key === 'Enter') {
    event.preventDefault()
    const val = parseInt((event.target as HTMLInputElement).value) || 0
    saveInlineField(trayId, 'min_stock', val)
    // Advance to fill-when-below input
    nextTick(() => {
      const el = document.getElementById(`fill-below-${trayId}`) as HTMLInputElement | null
      el?.focus()
      el?.select()
    })
  }
  if (event.key === 'Escape') {
    (event.target as HTMLInputElement).blur()
  }
}

function handleFillBelowKeydown(event: KeyboardEvent, trayId: string) {
  if (event.key === 'Enter') {
    event.preventDefault()
    const val = parseInt((event.target as HTMLInputElement).value) || 0
    saveInlineField(trayId, 'fill_when_below', val)
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

// One-click "Full" refill (no warehouse deduction — that happens at the packing list stage)
async function handleRefillFull(trayId: string) {
  await refillToFull(trayId, machine.value.id)
}

// Quick +/- stock adjustment (mobile)
async function adjustStock(trayId: string, delta: number) {
  const tray = trays.value.find(t => t.id === trayId)
  if (!tray) return
  const newVal = Math.max(0, Math.min(tray.capacity, tray.current_stock + delta))
  if (newVal === tray.current_stock) return
  tray.current_stock = newVal // optimistic
  try {
    await updateTray(trayId, machine.value.id, { current_stock: newVal })
  } catch {
    // reverts via fetchTrays
  }
}

// Mobile: expanded tray for threshold editing
const expandedMobileTray = ref<string | null>(null)

// Refill all below-minimum trays
// When coming from the packing list flow, only refill by the packed amount
const refillAllLoading = ref(false)
const packedQuantities = ref<Record<string, number> | null>(null)

// Read packed quantities from sessionStorage (set by index page's packing list)
onMounted(() => {
  const machineId = route.params.id as string
  const key = `refill-packed-${machineId}`
  const stored = sessionStorage.getItem(key)
  if (stored) {
    try {
      packedQuantities.value = JSON.parse(stored)
    } catch { /* ignore */ }
    sessionStorage.removeItem(key)
  }
})

async function handleRefillAll() {
  refillAllLoading.value = true
  try {
    await refillAll(machine.value.id, packedQuantities.value ?? undefined)
    // Clear packed quantities after successful refill
    packedQuantities.value = null
  } finally {
    refillAllLoading.value = false
  }
}

async function handleDeleteTray(trayId: string) {
  try {
    await deleteTray(trayId, machine.value.id)
  } catch {
    // silent
  }
}

// Summary computed
const lowStockCount = computed(() =>
  trays.value.filter(t => t.min_stock > 0 && t.current_stock <= t.min_stock).length
)

const fillBelowCount = computed(() =>
  trays.value.filter(t =>
    !isLowStock(t) && t.fill_when_below > 0 && t.current_stock <= t.fill_when_below && t.current_stock > 0
  ).length
)

// Packing list: group needed items by product for low-stock and fill-when-below trays, ordered by first slot appearance
const packingList = computed(() => {
  const hasCritical = trays.value.some(t => isLowStock(t))
  const map = new Map<string, { product_id: string | null; name: string; needed: number; packed: number | null; image_url: string | null; firstSlot: number; critical: boolean }>()
  for (const tray of trays.value) {
    const critical = isLowStock(tray)
    const soft = hasCritical && isFillBelow(tray)
    if (!critical && !soft) continue
    const deficit = tray.capacity - tray.current_stock
    if (deficit <= 0) continue
    const key = tray.product_id || `slot-${tray.item_number}`
    const name = tray.product_name || `Slot #${tray.item_number}`
    const existing = map.get(key)
    if (existing) {
      existing.needed += deficit
      if (critical) existing.critical = true
    } else {
      const product = products.value.find(p => p.id === tray.product_id)
      map.set(key, { product_id: tray.product_id, name, needed: deficit, packed: null, image_url: product?.image_url ?? null, firstSlot: tray.item_number, critical })
    }
  }
  // When packed quantities are available, annotate each item with the packed amount
  if (packedQuantities.value) {
    for (const item of map.values()) {
      item.packed = item.product_id ? (packedQuantities.value[item.product_id] ?? 0) : null
    }
  }
  return Array.from(map.values()).sort((a, b) => a.firstSlot - b.firstSlot)
})

const isRefillMode = computed(() => route.query.tab === 'stock')

function isLowStock(tray: any) {
  return tray.min_stock > 0 && tray.current_stock <= tray.min_stock
}

function isFillBelow(tray: any) {
  return !isLowStock(tray) && tray.fill_when_below > 0 && tray.current_stock <= tray.fill_when_below && tray.current_stock > 0
}

function isHealthyInRefillMode(tray: any) {
  return isRefillMode.value && !isLowStock(tray) && !isFillBelow(tray) && tray.current_stock > 0
}

function trayDeficit(tray: any) {
  return Math.max(0, tray.capacity - tray.current_stock)
}

function stockPercent(tray: any) {
  if (tray.capacity === 0) return 0
  return Math.round((tray.current_stock / tray.capacity) * 100)
}

function minStockPercent(tray: any) {
  if (tray.capacity === 0 || !tray.min_stock) return 0
  return Math.round((tray.min_stock / tray.capacity) * 100)
}

function fillBelowPercent(tray: any) {
  if (tray.capacity === 0 || !tray.fill_when_below) return 0
  return Math.round((tray.fill_when_below / tray.capacity) * 100)
}

function stockColor(tray: any) {
  if (isLowStock(tray)) return 'bg-red-500'
  if (isFillBelow(tray)) return 'bg-amber-500'
  const pct = stockPercent(tray)
  if (pct > 50) return 'bg-green-500'
  if (pct > 20) return 'bg-yellow-500'
  return 'bg-red-500'
}
</script>

<template>
  <div class="flex flex-1 flex-col gap-6 overflow-x-hidden p-4 md:p-6">
        <div v-if="loading" class="text-muted-foreground">Loading…</div>
        <div v-else-if="errorMsg" class="text-destructive">{{ errorMsg }}</div>
        <template v-else-if="machine">
          <!-- Machine info -->
          <div class="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
            <div class="min-w-0">
              <div v-if="editingName" class="flex items-center gap-2">
                <input
                  id="machine-name-input"
                  v-model="editName"
                  type="text"
                  class="h-9 w-full rounded-md border bg-transparent px-3 text-xl font-semibold shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring sm:text-2xl"
                  @keydown.enter="saveNameEdit"
                  @keydown.escape="cancelEditName"
                  @blur="saveNameEdit"
                />
              </div>
              <h1
                v-else
                class="group flex cursor-pointer items-center gap-2 text-xl font-semibold sm:text-2xl"
                @click="startEditName"
              >
                {{ machine.name ?? 'Unnamed machine' }}
                <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4 text-muted-foreground opacity-0 transition-opacity group-hover:opacity-100" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 3a2.85 2.83 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5Z"/><path d="m15 5 4 4"/></svg>
              </h1>
              <p v-if="machine.location_lat && machine.location_lon" class="mt-1 text-sm text-muted-foreground">
                {{ machine.location_lat.toFixed(5) }}, {{ machine.location_lon.toFixed(5) }}
              </p>
            </div>
            <!-- Device: compact header row -->
            <div class="flex items-center gap-2 shrink-0">
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
                <button
                  v-if="isAdmin"
                  class="inline-flex items-center gap-1 rounded-md bg-primary px-3 py-1.5 text-xs font-medium text-primary-foreground shadow-sm transition-colors hover:bg-primary/90"
                  @click="openCreditModal"
                >
                  <IconSend class="size-3" />
                  <span class="hidden sm:inline">Send Credit</span>
                </button>
                <button
                  class="inline-flex h-8 w-8 items-center justify-center rounded-md border text-muted-foreground transition-colors hover:bg-muted hover:text-foreground"
                  title="Device settings"
                  @click="showDeviceInfoModal = true"
                >
                  <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/></svg>
                </button>
              </template>
              <template v-else>
                <span class="rounded-full bg-muted px-3 py-1 text-xs font-medium text-muted-foreground">
                  No device
                </span>
                <button
                  v-if="isAdmin"
                  class="text-xs text-primary hover:underline"
                  @click="openDeviceModal"
                >
                  Assign device
                </button>
              </template>
            </div>
          </div>

          <!-- Tabs: Sales | Trays & Stock | MDB -->
          <Tabs :default-value="defaultTab">
            <TabsList>
              <TabsTrigger value="sales">Sales</TabsTrigger>
              <TabsTrigger v-if="isAdmin" value="mdb">MDB</TabsTrigger>
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
                    class="group rounded-lg border bg-card p-4 transition-colors hover:bg-muted/40"
                  >
                    <div class="flex items-center gap-3">
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
                        <p class="font-medium break-words">
                          {{ trayProductMap.get(sale.item_number)?.name ?? `Item #${sale.item_number}` }}
                        </p>
                        <div class="mt-1 flex flex-wrap items-center gap-x-2 gap-y-1 text-xs text-muted-foreground">
                          <span>Slot {{ sale.item_number }}</span>
                          <span class="text-muted-foreground/40">·</span>
                          <span
                            class="inline-flex items-center gap-1 rounded-full px-2 py-0.5 text-[10px] font-medium uppercase tracking-wide"
                            :class="sale.channel === 'card'
                              ? 'bg-blue-500/10 text-blue-600 dark:text-blue-400'
                              : 'bg-emerald-500/10 text-emerald-600 dark:text-emerald-400'"
                          >
                            <IconCreditCard v-if="sale.channel === 'card'" class="size-3.5" />
                            <IconCoins v-else class="size-3.5" />
                            {{ sale.channel }}
                          </span>
                        </div>
                      </div>
                      <!-- Price + Timestamp -->
                      <div class="shrink-0 text-right">
                        <span class="text-sm font-medium">{{ formatCurrency(sale.item_price) }}</span>
                        <p class="mt-0.5 text-[11px] text-muted-foreground">{{ new Date(sale.created_at).toLocaleString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit' }) }}</p>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </TabsContent>

            <!-- Trays & Stock tab -->
            <TabsContent value="trays" class="mt-4">
              <!-- Refill summary banner + packing list -->
              <div v-if="lowStockCount > 0 || (fillBelowCount > 0 && lowStockCount > 0)" class="mb-4 rounded-lg border border-amber-300 bg-amber-50 dark:border-amber-700 dark:bg-amber-950/30">
                <!-- Header row -->
                <div class="flex items-center gap-3 px-4 py-3">
                  <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5 shrink-0 text-amber-600 dark:text-amber-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" x2="12" y1="9" y2="13"/><line x1="12" x2="12.01" y1="17" y2="17"/></svg>
                  <span class="text-sm font-medium text-amber-800 dark:text-amber-200">
                    {{ lowStockCount }} tray{{ lowStockCount > 1 ? 's' : '' }} below minimum
                    <template v-if="fillBelowCount > 0">
                      <span class="text-blue-700 dark:text-blue-300">+ {{ fillBelowCount }} below fill threshold</span>
                    </template>
                  </span>
                  <button
                    v-if="isAdmin"
                    :disabled="refillAllLoading"
                    class="ml-auto inline-flex h-8 items-center gap-1.5 rounded-md bg-amber-600 px-3 text-xs font-medium text-white shadow-sm transition-colors hover:bg-amber-700 disabled:opacity-50 dark:bg-amber-600 dark:hover:bg-amber-500"
                    @click="handleRefillAll"
                  >
                    <svg xmlns="http://www.w3.org/2000/svg" class="h-3.5 w-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" x2="12" y1="3" y2="15"/></svg>
                    <span v-if="refillAllLoading">Refilling…</span>
                    <span v-else-if="packedQuantities">Refill packed ({{ lowStockCount + fillBelowCount }})</span>
                    <span v-else>Refill all ({{ lowStockCount + fillBelowCount }})</span>
                  </button>
                </div>
                <!-- Packing list -->
                <div v-if="packingList.length > 0" class="flex flex-wrap gap-2 border-t border-amber-200 px-4 py-3 dark:border-amber-800">
                  <div
                    v-for="item in packingList"
                    :key="item.name"
                    class="inline-flex max-w-full items-center gap-2 rounded-full py-1 pl-1 pr-3 text-sm shadow-sm"
                    :class="item.packed === 0
                      ? 'bg-white/40 opacity-50 dark:bg-black/10'
                      : 'bg-white/70 dark:bg-black/20'"
                  >
                    <img
                      v-if="item.image_url"
                      :src="item.image_url"
                      :alt="item.name"
                      class="h-6 w-6 rounded-full object-cover"
                    />
                    <div v-else class="flex h-6 w-6 items-center justify-center rounded-full bg-amber-200 text-[10px] font-bold text-amber-700 dark:bg-amber-800 dark:text-amber-200">
                      {{ item.name.charAt(0) }}
                    </div>
                    <span class="truncate font-medium text-amber-900 dark:text-amber-100">{{ item.name }}</span>
                    <!-- Packed quantities indicator -->
                    <template v-if="item.packed !== null">
                      <span v-if="item.packed === 0" class="rounded-full bg-red-500 px-1.5 py-0.5 text-[11px] font-semibold leading-none text-white">0/{{ item.needed }}</span>
                      <span v-else-if="item.packed < item.needed" class="rounded-full bg-amber-500 px-1.5 py-0.5 text-[11px] font-semibold leading-none text-white">{{ item.packed }}/{{ item.needed }}</span>
                      <span v-else class="rounded-full bg-green-600 px-1.5 py-0.5 text-[11px] font-semibold leading-none text-white">{{ item.packed }}</span>
                    </template>
                    <!-- Default: no packed quantities -->
                    <span
                      v-else
                      class="rounded-full px-1.5 py-0.5 text-[11px] font-semibold leading-none text-white"
                      :class="item.critical ? 'bg-amber-600' : 'bg-blue-500'"
                    >{{ item.needed }}</span>
                  </div>
                </div>
              </div>

              <div class="mb-4 flex flex-col gap-2 sm:flex-row sm:items-center sm:justify-between">
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
              <template v-else>
                <!-- ── Mobile card layout ── -->
                <div class="space-y-3 md:hidden">
                  <div
                    v-for="tray in trays"
                    :key="'m-' + tray.id"
                    class="rounded-lg border p-3 transition-colors"
                    :class="[
                      isLowStock(tray) ? 'border-amber-300 bg-amber-50/60 dark:border-amber-700 dark:bg-amber-950/20'
                        : isFillBelow(tray) && lowStockCount > 0 ? 'border-blue-300 bg-blue-50/40 dark:border-blue-700 dark:bg-blue-950/10'
                        : 'bg-card',
                      isHealthyInRefillMode(tray) ? 'opacity-40' : '',
                    ]"
                  >
                    <!-- Row 1: image + slot + product + actions -->
                    <div class="flex items-center gap-3">
                      <!-- Product image -->
                      <img
                        v-if="trayProductMap.get(tray.item_number)?.image_url"
                        :src="trayProductMap.get(tray.item_number)!.image_url!"
                        :alt="tray.product_name ?? ''"
                        class="h-10 w-10 shrink-0 rounded-lg object-cover"
                      />
                      <div v-else class="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-muted text-xs font-semibold text-muted-foreground">
                        {{ tray.item_number }}
                      </div>
                      <!-- Product name + slot -->
                      <div class="min-w-0 flex-1">
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
                            class="block truncate text-sm font-medium transition-colors hover:text-primary"
                            @click="openProductAutocomplete(tray)"
                          >
                            {{ tray.product_name ?? '—' }}
                          </button>
                        </template>
                        <span v-else class="block truncate text-sm font-medium">{{ tray.product_name ?? '—' }}</span>
                        <span class="text-xs text-muted-foreground">
                          Slot {{ tray.item_number }}
                          <template v-if="trayProductMap.get(tray.item_number)?.sellprice">
                            &middot; {{ formatCurrency(trayProductMap.get(tray.item_number)!.sellprice!) }}
                          </template>
                        </span>
                      </div>
                      <!-- Actions: Full only on mobile -->
                      <button
                        v-if="isAdmin"
                        class="inline-flex h-8 shrink-0 items-center rounded-md px-3 text-xs font-medium transition-colors"
                        :class="tray.current_stock < tray.capacity
                          ? 'bg-primary/10 text-primary hover:bg-primary/20'
                          : 'text-muted-foreground cursor-default opacity-50'"
                        :disabled="tray.current_stock >= tray.capacity"
                        @click="handleRefillFull(tray.id)"
                      >
                        Full
                      </button>
                    </div>
                    <!-- Row 2: level bar -->
                    <div class="relative mt-2 h-2 w-full rounded-full bg-muted">
                      <div
                        class="h-2 rounded-full transition-all"
                        :class="stockColor(tray)"
                        :style="{ width: `${stockPercent(tray)}%` }"
                      />
                      <div
                        v-if="tray.min_stock > 0 && minStockPercent(tray) > 0 && minStockPercent(tray) < 100"
                        class="absolute top-0 h-2 w-0.5 bg-amber-600 dark:bg-amber-400"
                        :style="{ left: `${minStockPercent(tray)}%` }"
                      />
                      <div
                        v-if="tray.fill_when_below > 0 && fillBelowPercent(tray) > 0 && fillBelowPercent(tray) < 100"
                        class="absolute top-0 h-2 w-0.5 bg-blue-500 dark:bg-blue-400"
                        :style="{ left: `${fillBelowPercent(tray)}%` }"
                      />
                    </div>
                    <!-- Row 3: +/- stock controls + info -->
                    <div class="mt-2 flex items-center justify-between">
                      <template v-if="isAdmin">
                        <div class="flex items-center gap-2">
                          <button
                            class="inline-flex h-8 w-8 items-center justify-center rounded-md border text-lg font-medium transition-colors hover:bg-muted active:bg-muted/80"
                            :disabled="tray.current_stock <= 0"
                            :class="tray.current_stock <= 0 ? 'opacity-30' : ''"
                            @click="adjustStock(tray.id, -1)"
                          >
                            −
                          </button>
                          <span class="min-w-[3.5rem] text-center text-sm font-semibold tabular-nums">
                            {{ tray.current_stock }} / {{ tray.capacity }}
                          </span>
                          <button
                            class="inline-flex h-8 w-8 items-center justify-center rounded-md border text-lg font-medium transition-colors hover:bg-muted active:bg-muted/80"
                            :disabled="tray.current_stock >= tray.capacity"
                            :class="tray.current_stock >= tray.capacity ? 'opacity-30' : ''"
                            @click="adjustStock(tray.id, 1)"
                          >
                            +
                          </button>
                        </div>
                      </template>
                      <span v-else class="text-xs text-muted-foreground">{{ tray.current_stock }} / {{ tray.capacity }}</span>
                      <div class="flex items-center gap-2 text-xs text-muted-foreground">
                        <button
                          v-if="isAdmin"
                          type="button"
                          class="inline-flex items-center gap-1 rounded px-1 py-0.5 transition-colors hover:bg-muted active:bg-muted/80"
                          @click="expandedMobileTray = expandedMobileTray === tray.id ? null : tray.id"
                        >
                          <span v-if="tray.min_stock">Min: {{ tray.min_stock }}</span>
                          <span v-if="tray.fill_when_below">Fill: {{ tray.fill_when_below }}</span>
                          <span v-if="!tray.min_stock && !tray.fill_when_below" class="italic">Set thresholds</span>
                          <svg
                            xmlns="http://www.w3.org/2000/svg" class="h-3 w-3 transition-transform" :class="expandedMobileTray === tray.id ? 'rotate-180' : ''"
                            viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
                          ><polyline points="6 9 12 15 18 9" /></svg>
                        </button>
                        <template v-else>
                          <span v-if="tray.min_stock">Min: {{ tray.min_stock }}</span>
                          <span v-if="tray.fill_when_below">Fill: {{ tray.fill_when_below }}</span>
                        </template>
                        <span
                          v-if="trayDeficit(tray) > 0 && isLowStock(tray)"
                          class="font-semibold text-red-600 dark:text-red-400"
                        >
                          -{{ trayDeficit(tray) }}
                        </span>
                        <span
                          v-else-if="trayDeficit(tray) > 0 && isFillBelow(tray) && lowStockCount > 0"
                          class="font-semibold text-blue-600 dark:text-blue-400"
                        >
                          -{{ trayDeficit(tray) }}
                        </span>
                      </div>
                    </div>
                    <!-- Expandable thresholds row (mobile, admin only) -->
                    <div
                      v-if="isAdmin && expandedMobileTray === tray.id"
                      class="mt-2 flex items-center gap-4 rounded-md bg-muted/50 px-3 py-2"
                    >
                      <label class="flex items-center gap-1.5 text-xs text-muted-foreground">
                        Min
                        <input
                          type="number"
                          :value="tray.min_stock"
                          min="0"
                          :max="tray.capacity"
                          class="h-7 w-14 rounded border border-input bg-background px-1.5 text-center text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                          @change="(e: Event) => saveInlineField(tray.id, 'min_stock', parseInt((e.target as HTMLInputElement).value) || 0)"
                        />
                      </label>
                      <label class="flex items-center gap-1.5 text-xs text-muted-foreground">
                        Fill
                        <input
                          type="number"
                          :value="tray.fill_when_below"
                          min="0"
                          :max="tray.capacity"
                          class="h-7 w-14 rounded border border-input bg-background px-1.5 text-center text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                          @change="(e: Event) => saveInlineField(tray.id, 'fill_when_below', parseInt((e.target as HTMLInputElement).value) || 0)"
                        />
                      </label>
                    </div>
                  </div>
                </div>

                <!-- ── Desktop table layout ── -->
                <div class="hidden md:block rounded-md border overflow-visible">
                  <table class="w-full text-sm">
                    <thead>
                      <tr class="border-b bg-muted/50 text-left">
                        <th class="w-20 px-4 py-3 font-medium">Slot #</th>
                        <th class="px-4 py-3 font-medium">Product</th>
                        <th class="w-36 px-4 py-3 font-medium">Stock</th>
                        <th class="w-16 px-4 py-3 font-medium">
                          <TooltipProvider>
                            <Tooltip>
                              <TooltipTrigger as-child>
                                <span class="inline-flex cursor-help items-center gap-1">
                                  Min
                                  <span class="inline-flex h-3.5 w-3.5 items-center justify-center rounded-full bg-muted-foreground/20 text-[9px] font-semibold leading-none text-muted-foreground">i</span>
                                </span>
                              </TooltipTrigger>
                              <TooltipContent>
                                <p class="max-w-48">Minimum stock. Triggers a refill alert when stock drops to or below this number.</p>
                              </TooltipContent>
                            </Tooltip>
                          </TooltipProvider>
                        </th>
                        <th class="w-16 px-4 py-3 font-medium">
                          <TooltipProvider>
                            <Tooltip>
                              <TooltipTrigger as-child>
                                <span class="inline-flex cursor-help items-center gap-1">
                                  Fill
                                  <span class="inline-flex h-3.5 w-3.5 items-center justify-center rounded-full bg-muted-foreground/20 text-[9px] font-semibold leading-none text-muted-foreground">i</span>
                                </span>
                              </TooltipTrigger>
                              <TooltipContent>
                                <p class="max-w-48">Fill threshold. When already restocking, also top up trays below this level.</p>
                              </TooltipContent>
                            </Tooltip>
                          </TooltipProvider>
                        </th>
                        <th class="w-32 px-4 py-3 font-medium">Level</th>
                        <th v-if="isAdmin" class="w-24 px-4 py-3 font-medium">Actions</th>
                      </tr>
                    </thead>
                    <tbody>
                      <tr
                        v-for="tray in trays"
                        :key="tray.id"
                        class="border-b last:border-0 transition-colors"
                        :class="[
                          isLowStock(tray) ? 'bg-amber-50/60 hover:bg-amber-100/60 dark:bg-amber-950/20 dark:hover:bg-amber-950/40'
                            : isFillBelow(tray) && lowStockCount > 0 ? 'bg-blue-50/40 hover:bg-blue-100/40 dark:bg-blue-950/10 dark:hover:bg-blue-950/20'
                            : 'hover:bg-muted/30',
                          isHealthyInRefillMode(tray) ? 'opacity-40' : '',
                        ]"
                      >
                        <!-- Slot # + price (read-only) -->
                        <td class="px-4 py-2">
                          <span class="font-mono">{{ tray.item_number }}</span>
                          <span v-if="trayProductMap.get(tray.item_number)?.sellprice" class="ml-1 text-xs text-muted-foreground">
                            {{ formatCurrency(trayProductMap.get(tray.item_number)!.sellprice!) }}
                          </span>
                        </td>

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
                              <span
                                v-if="trayDeficit(tray) > 0 && isLowStock(tray)"
                                class="ml-1 text-xs font-semibold text-red-600 dark:text-red-400"
                                :title="`${trayDeficit(tray)} items needed to fill`"
                              >
                                -{{ trayDeficit(tray) }}
                              </span>
                              <span
                                v-else-if="trayDeficit(tray) > 0 && isFillBelow(tray) && lowStockCount > 0"
                                class="ml-1 text-xs font-semibold text-blue-600 dark:text-blue-400"
                                :title="`${trayDeficit(tray)} items to top up`"
                              >
                                -{{ trayDeficit(tray) }}
                              </span>
                            </div>
                          </template>
                          <div v-else class="flex items-center gap-1">
                            <span class="text-muted-foreground">{{ tray.current_stock }} / {{ tray.capacity }}</span>
                            <span
                              v-if="trayDeficit(tray) > 0 && isLowStock(tray)"
                              class="ml-1 text-xs font-semibold text-red-600 dark:text-red-400"
                            >
                              -{{ trayDeficit(tray) }}
                            </span>
                            <span
                              v-else-if="trayDeficit(tray) > 0 && isFillBelow(tray) && lowStockCount > 0"
                              class="ml-1 text-xs font-semibold text-blue-600 dark:text-blue-400"
                            >
                              -{{ trayDeficit(tray) }}
                            </span>
                          </div>
                        </td>

                        <!-- Min stock threshold -->
                        <td class="px-4 py-2">
                          <template v-if="isAdmin">
                            <input
                              :id="`min-stock-${tray.id}`"
                              type="number"
                              :value="tray.min_stock"
                              min="0"
                              :max="tray.capacity"
                              class="h-7 w-12 rounded border border-transparent bg-transparent px-1 text-center text-sm hover:border-input focus:border-input focus:bg-background focus:shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                              @change="(e: Event) => saveInlineField(tray.id, 'min_stock', parseInt((e.target as HTMLInputElement).value) || 0)"
                              @keydown="(e: KeyboardEvent) => handleMinStockKeydown(e, tray.id)"
                            />
                          </template>
                          <span v-else class="text-muted-foreground">{{ tray.min_stock || '—' }}</span>
                        </td>

                        <!-- Fill when below threshold -->
                        <td class="px-4 py-2">
                          <template v-if="isAdmin">
                            <input
                              :id="`fill-below-${tray.id}`"
                              type="number"
                              :value="tray.fill_when_below"
                              min="0"
                              :max="tray.capacity"
                              class="h-7 w-12 rounded border border-transparent bg-transparent px-1 text-center text-sm hover:border-input focus:border-input focus:bg-background focus:shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                              @change="(e: Event) => saveInlineField(tray.id, 'fill_when_below', parseInt((e.target as HTMLInputElement).value) || 0)"
                              @keydown="(e: KeyboardEvent) => handleFillBelowKeydown(e, tray.id)"
                            />
                          </template>
                          <span v-else class="text-muted-foreground">{{ tray.fill_when_below || '—' }}</span>
                        </td>

                        <!-- Level bar with threshold markers -->
                        <td class="px-4 py-2">
                          <div class="relative h-2 w-full rounded-full bg-muted">
                            <div
                              class="h-2 rounded-full transition-all"
                              :class="stockColor(tray)"
                              :style="{ width: `${stockPercent(tray)}%` }"
                            />
                            <div
                              v-if="tray.min_stock > 0 && minStockPercent(tray) > 0 && minStockPercent(tray) < 100"
                              class="absolute top-0 h-2 w-0.5 bg-amber-600 dark:bg-amber-400"
                              :style="{ left: `${minStockPercent(tray)}%` }"
                              :title="`Min stock: ${tray.min_stock}`"
                            />
                            <div
                              v-if="tray.fill_when_below > 0 && fillBelowPercent(tray) > 0 && fillBelowPercent(tray) < 100"
                              class="absolute top-0 h-2 w-0.5 bg-blue-500 dark:bg-blue-400"
                              :style="{ left: `${fillBelowPercent(tray)}%` }"
                              :title="`Fill when below: ${tray.fill_when_below}`"
                            />
                          </div>
                        </td>

                        <!-- Actions (Full + Remove) -->
                        <td v-if="isAdmin" class="px-4 py-2">
                          <div class="flex items-center gap-2">
                            <button
                              class="inline-flex h-7 items-center rounded px-2 text-xs font-medium transition-colors"
                              :class="tray.current_stock < tray.capacity
                                ? 'bg-primary/10 text-primary hover:bg-primary/20'
                                : 'text-muted-foreground cursor-default opacity-50'"
                              :disabled="tray.current_stock >= tray.capacity"
                              @click="handleRefillFull(tray.id)"
                            >
                              Full
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
              </template>

                <!-- Done navigation (visible in refill mode) -->
                <div v-if="isRefillMode" class="mt-6 flex justify-center">
                  <NuxtLink
                    to="/machines"
                    class="inline-flex h-10 items-center justify-center rounded-md border px-6 text-sm font-medium hover:bg-muted"
                  >
                    &larr; Done — back to machines
                  </NuxtLink>
                </div>
            </TabsContent>

            <!-- ── MDB Diagnostics Tab ── -->
            <TabsContent v-if="isAdmin" value="mdb" class="mt-4 space-y-6">

              <!-- Current MDB Status Card -->
              <div class="rounded-xl border bg-card p-6">
                <h2 class="mb-4 text-sm font-medium text-muted-foreground uppercase tracking-wide">Current MDB Status</h2>
                <template v-if="machine.embeddeds?.mdb_diagnostics">
                  <div class="grid grid-cols-2 gap-4 sm:grid-cols-3 lg:grid-cols-5">
                    <div>
                      <p class="text-xs text-muted-foreground">State</p>
                      <span
                        class="mt-1 inline-flex items-center rounded-full px-2.5 py-0.5 text-xs font-medium"
                        :class="{
                          'bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400': stateVariant(machine.embeddeds.mdb_diagnostics.state) === 'destructive',
                          'bg-gray-100 text-gray-800 dark:bg-gray-800 dark:text-gray-300': stateVariant(machine.embeddeds.mdb_diagnostics.state) === 'outline',
                          'bg-blue-100 text-blue-800 dark:bg-blue-900/30 dark:text-blue-400': stateVariant(machine.embeddeds.mdb_diagnostics.state) === 'secondary',
                          'bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400': stateVariant(machine.embeddeds.mdb_diagnostics.state) === 'default',
                        }"
                      >
                        {{ stateLabel(machine.embeddeds.mdb_diagnostics.state) }}
                      </span>
                    </div>
                    <div>
                      <p class="text-xs text-muted-foreground">Address</p>
                      <p class="mt-1 text-sm font-mono font-medium">{{ machine.embeddeds.mdb_diagnostics.addr }}</p>
                    </div>
                    <div>
                      <p class="text-xs text-muted-foreground">Polls</p>
                      <p class="mt-1 text-sm font-medium">{{ Number(machine.embeddeds.mdb_diagnostics.polls ?? 0).toLocaleString() }}</p>
                    </div>
                    <div>
                      <p class="text-xs text-muted-foreground">Checksum Errors</p>
                      <p class="mt-1 text-sm font-medium" :class="machine.embeddeds.mdb_diagnostics.chkErr > 0 ? 'text-red-500' : ''">
                        {{ machine.embeddeds.mdb_diagnostics.chkErr ?? 0 }}
                      </p>
                    </div>
                    <div>
                      <p class="text-xs text-muted-foreground">Last Command</p>
                      <p class="mt-1 text-sm font-mono">{{ machine.embeddeds.mdb_diagnostics.lastCmd }}</p>
                    </div>
                  </div>
                  <p class="mt-3 text-xs text-muted-foreground">
                    Updated {{ timeAgo(machine.embeddeds.mdb_diagnostics.updated_at) }}
                  </p>
                </template>
                <p v-else class="text-sm text-muted-foreground">
                  No MDB diagnostics received yet. The device will start reporting after connecting.
                </p>
              </div>

              <!-- State Change History -->
              <div>
                <h2 class="mb-3 text-sm font-medium text-muted-foreground uppercase tracking-wide">State Change History</h2>

                <div v-if="mdbLogs.length === 0 && !mdbLogsLoading" class="rounded-xl border bg-card p-6 text-center text-sm text-muted-foreground">
                  No state changes recorded yet.
                </div>

                <div v-else class="space-y-2">
                  <div
                    v-for="entry in mdbLogs"
                    :key="entry.id"
                    class="flex items-center gap-3 rounded-lg border bg-card px-4 py-3"
                  >
                    <!-- State badge -->
                    <span
                      class="inline-flex shrink-0 items-center rounded-full px-2.5 py-0.5 text-xs font-medium"
                      :class="{
                        'bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400': stateVariant(entry.state) === 'destructive',
                        'bg-gray-100 text-gray-800 dark:bg-gray-800 dark:text-gray-300': stateVariant(entry.state) === 'outline',
                        'bg-blue-100 text-blue-800 dark:bg-blue-900/30 dark:text-blue-400': stateVariant(entry.state) === 'secondary',
                        'bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400': stateVariant(entry.state) === 'default',
                      }"
                    >
                      {{ stateLabel(entry.state) }}
                    </span>

                    <!-- Transition description -->
                    <div class="min-w-0 flex-1">
                      <p class="text-sm">
                        <template v-if="entry.prev_state">
                          {{ stateLabel(entry.prev_state) }} &rarr; {{ stateLabel(entry.state) }}
                        </template>
                        <template v-else>
                          Initial: {{ stateLabel(entry.state) }}
                        </template>
                      </p>
                      <p class="text-xs text-muted-foreground">
                        <span v-if="entry.last_cmd">Cmd: {{ entry.last_cmd }}</span>
                        <span v-if="entry.last_cmd && entry.polls != null"> · </span>
                        <span v-if="entry.polls != null">{{ entry.polls.toLocaleString() }} polls</span>
                        <span v-if="entry.chk_err"> · {{ entry.chk_err }} errors</span>
                      </p>
                    </div>

                    <!-- Timestamp -->
                    <p class="shrink-0 text-xs text-muted-foreground">
                      {{ timeAgo(entry.created_at) }}
                    </p>
                  </div>
                </div>

                <!-- Load more -->
                <div v-if="mdbHasMore && mdbLogs.length > 0" class="mt-4 text-center">
                  <button
                    class="text-sm text-primary hover:underline disabled:opacity-50"
                    :disabled="mdbLogsLoading"
                    @click="machine.embeddeds?.id && fetchMoreMdbLogs(machine.embeddeds.id)"
                  >
                    {{ mdbLogsLoading ? 'Loading...' : 'Load more' }}
                  </button>
                </div>
              </div>

            </TabsContent>
          </Tabs>
        </template>
      </div>

      <!-- Device info modal -->
      <div
        v-if="showDeviceInfoModal && machine?.embeddeds"
        class="fixed inset-0 z-50 flex items-end sm:items-center justify-center bg-black/40"
        @click.self="showDeviceInfoModal = false"
      >
        <div class="w-full max-w-sm rounded-t-xl sm:rounded-xl border bg-card p-5 sm:p-6 shadow-lg">
          <div class="flex items-center justify-between mb-4">
            <h2 class="text-lg font-semibold">Device Details</h2>
            <button
              class="inline-flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-muted"
              @click="showDeviceInfoModal = false"
            >
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
            </button>
          </div>

          <div class="space-y-3 text-sm">
            <!-- Status -->
            <div class="flex justify-between items-center">
              <span class="text-muted-foreground">Status</span>
              <span
                class="rounded-full px-2.5 py-0.5 text-xs font-medium"
                :class="{
                  'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400': machine.embeddeds.status === 'online' || machine.embeddeds.status === 'ota_success',
                  'bg-yellow-100 text-yellow-700 dark:bg-yellow-900/30 dark:text-yellow-400': machine.embeddeds.status === 'ota_updating',
                  'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400': machine.embeddeds.status === 'ota_failed',
                  'bg-muted text-muted-foreground': !['online', 'ota_updating', 'ota_success', 'ota_failed'].includes(machine.embeddeds.status),
                }"
              >
                {{ machine.embeddeds.status === 'ota_updating' ? 'updating' : machine.embeddeds.status === 'ota_success' ? 'updated' : machine.embeddeds.status === 'ota_failed' ? 'update failed' : machine.embeddeds.status }}
              </span>
            </div>

            <!-- MAC Address -->
            <div class="flex justify-between items-center">
              <span class="text-muted-foreground">MAC Address</span>
              <span class="font-mono text-xs">{{ machine.embeddeds.mac_address ?? '—' }}</span>
            </div>

            <!-- Subdomain -->
            <div class="flex justify-between items-center">
              <span class="text-muted-foreground">Subdomain</span>
              <span class="font-mono text-xs">{{ machine.embeddeds.subdomain }}</span>
            </div>

            <!-- Firmware -->
            <div class="flex justify-between items-center">
              <span class="text-muted-foreground">Firmware</span>
              <span v-if="machine.embeddeds.firmware_version" class="text-right">
                <span class="font-mono text-xs">v{{ machine.embeddeds.firmware_version }}</span>
                <span v-if="machine.embeddeds.firmware_build_date" class="block text-xs text-muted-foreground">
                  built {{ new Date(machine.embeddeds.firmware_build_date).toLocaleDateString() }}
                </span>
              </span>
              <span v-else class="text-xs text-muted-foreground">—</span>
            </div>

            <!-- Last seen -->
            <div class="flex justify-between items-center">
              <span class="text-muted-foreground">Last Seen</span>
              <span class="text-xs">{{ formatDate(machine.embeddeds.status_at) }}</span>
            </div>

            <!-- MDB Address -->
            <div class="flex justify-between items-center">
              <span class="text-muted-foreground">MDB Address</span>
              <template v-if="isAdmin">
                <div class="flex items-center gap-2">
                  <div class="inline-flex rounded-md border">
                    <button
                      class="px-2.5 py-1 text-xs font-medium transition-colors rounded-l-md"
                      :class="((machine.embeddeds as any).mdb_address ?? 1) === 1
                        ? 'bg-primary text-primary-foreground'
                        : 'hover:bg-muted'"
                      :disabled="mdbAddressLoading"
                      @click="setMdbAddress(1)"
                    >
                      #1 (0x10)
                    </button>
                    <button
                      class="px-2.5 py-1 text-xs font-medium transition-colors rounded-r-md border-l"
                      :class="(machine.embeddeds as any).mdb_address === 2
                        ? 'bg-primary text-primary-foreground'
                        : 'hover:bg-muted'"
                      :disabled="mdbAddressLoading"
                      @click="setMdbAddress(2)"
                    >
                      #2 (0x60)
                    </button>
                  </div>
                  <span v-if="mdbAddressLoading" class="text-xs text-muted-foreground">...</span>
                </div>
              </template>
              <span v-else class="text-xs font-medium">
                #{{ (machine.embeddeds as any).mdb_address ?? 1 }} ({{ ((machine.embeddeds as any).mdb_address ?? 1) === 1 ? '0x10' : '0x60' }})
              </span>
            </div>
            <p v-if="mdbAddressError" class="text-xs text-destructive">{{ mdbAddressError }}</p>
          </div>

          <!-- Actions -->
          <div v-if="isAdmin" class="mt-5 flex gap-2 border-t pt-4">
            <button
              class="inline-flex h-9 flex-1 items-center justify-center rounded-md border text-sm font-medium transition-colors hover:bg-muted"
              @click="showDeviceInfoModal = false; openDeviceModal()"
            >
              Change Device
            </button>
            <button
              class="inline-flex h-9 items-center justify-center rounded-md px-4 text-sm font-medium text-destructive transition-colors hover:bg-destructive/10"
              :disabled="deviceSwapLoading"
              @click="detachDevice(); showDeviceInfoModal = false"
            >
              Detach
            </button>
          </div>
        </div>
      </div>

      <!-- Device swap/assign modal -->
      <div
        v-if="showDeviceModal"
        class="fixed inset-0 z-50 flex items-end sm:items-center justify-center bg-black/40"
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

      <!-- Send credit modal -->
      <div
        v-if="showCreditModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showCreditModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-2 text-lg font-semibold">Send Credit</h2>
          <p class="mb-4 text-sm text-muted-foreground">
            Send a credit amount to <span class="font-medium text-foreground">{{ machine?.name ?? 'this machine' }}</span>.
            The device will dispense the next item selected.
          </p>
          <form class="space-y-4" @submit.prevent="submitCredit">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="credit-amount">Amount (EUR)</label>
              <input
                id="credit-amount"
                v-model="creditAmount"
                type="number"
                step="0.01"
                min="0.01"
                placeholder="1.50"
                required
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <p v-if="creditError" class="text-sm text-destructive">{{ creditError }}</p>
            <p v-if="creditSuccess" class="text-sm text-green-600 dark:text-green-400">{{ creditSuccess }}</p>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showCreditModal = false"
              >
                Close
              </button>
              <button
                type="button"
                :disabled="cancelCreditLoading"
                class="inline-flex h-9 items-center justify-center gap-1.5 rounded-md border border-destructive px-4 text-sm font-medium text-destructive shadow-sm transition-colors hover:bg-destructive/10 disabled:opacity-50"
                @click="cancelCredit"
              >
                <span v-if="cancelCreditLoading">…</span>
                <span v-else>Cancel Credit</span>
              </button>
              <button
                type="submit"
                :disabled="creditLoading"
                class="inline-flex h-9 flex-1 items-center justify-center gap-1.5 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="creditLoading">Sending…</span>
                <template v-else>
                  <IconSend class="size-3.5" />
                  Send
                </template>
              </button>
            </div>
          </form>
        </div>
      </div>
</template>
