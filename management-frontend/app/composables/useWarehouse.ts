import { useSupabaseClient } from '#imports'
import { useOrganization } from './useOrganization'
import { getProductImageUrl } from './useProducts'

// ── Interfaces ──────────────────────────────────────────────────────────────

export interface Warehouse {
  id: string
  name: string
  address: string | null
  notes: string | null
}

export interface ProductBarcode {
  id: string
  product_id: string
  barcode: string
  format: string
  product_name?: string | null
}

export interface StockBatch {
  id: string
  warehouse_id: string
  product_id: string
  product_name: string | null
  product_image_path: string | null
  batch_number: string | null
  expiration_date: string
  quantity: number
}

export interface WarehouseTransaction {
  id: string
  created_at: string
  warehouse_id: string
  product_id: string
  product_name: string | null
  batch_id: string | null
  user_id: string | null
  transaction_type: string
  quantity_change: number
  quantity_before: number | null
  quantity_after: number | null
  batch_number: string | null
  expiration_date: string | null
  reference_id: string | null
  notes: string | null
  metadata: Record<string, unknown> | null
}

export interface WarehouseProductSummary {
  product_id: string
  product_name: string
  product_image_path: string | null
  total_quantity: number
  min_stock: number
  is_below_min: boolean
  earliest_expiration: string | null
  expiration_status: 'ok' | 'warning' | 'critical'
  batch_count: number
}

export interface MinStockEntry {
  id: string
  product_id: string
  warehouse_id: string
  min_quantity: number
}

// ── Helpers ─────────────────────────────────────────────────────────────────

export function expirationStatus(dateStr: string | null): 'ok' | 'warning' | 'critical' {
  if (!dateStr) return 'ok' // no expiration = always ok
  const today = new Date()
  today.setHours(0, 0, 0, 0)
  const exp = new Date(dateStr)
  const diffDays = Math.floor((exp.getTime() - today.getTime()) / (1000 * 60 * 60 * 24))
  if (diffDays < 7) return 'critical'
  if (diffDays <= 30) return 'warning'
  return 'ok'
}

export function expirationBadgeClass(status: 'ok' | 'warning' | 'critical'): string {
  switch (status) {
    case 'critical': return 'bg-red-100 text-red-700 dark:bg-red-950/40 dark:text-red-400'
    case 'warning': return 'bg-amber-100 text-amber-700 dark:bg-amber-950/40 dark:text-amber-400'
    case 'ok': return 'bg-green-100 text-green-700 dark:bg-green-950/40 dark:text-green-400'
  }
}

export function expirationLabel(status: 'ok' | 'warning' | 'critical'): string {
  switch (status) {
    case 'critical': return 'Critical'
    case 'warning': return 'Expiring soon'
    case 'ok': return 'OK'
  }
}

const PAGE_SIZE = 50

// ── Composable ──────────────────────────────────────────────────────────────

export function useWarehouse() {
  const supabase = useSupabaseClient()
  const { organization } = useOrganization()

  const warehouses = ref<Warehouse[]>([])
  const batches = ref<StockBatch[]>([])
  const transactions = ref<WarehouseTransaction[]>([])
  const productSummaries = ref<WarehouseProductSummary[]>([])
  const barcodes = ref<ProductBarcode[]>([])
  const minStocks = ref<MinStockEntry[]>([])
  const loading = ref(false)
  const transactionLoading = ref(false)
  const transactionHasMore = ref(false)
  const transactionOffset = ref(0)

  // ── Warehouse CRUD ──────────────────────────────────────────────────────

  async function fetchWarehouses() {
    const { data, error } = await (supabase as any)
      .from('warehouses')
      .select('id, name, address, notes')
      .order('name')
    if (error) throw error
    warehouses.value = (data ?? []) as Warehouse[]
  }

  async function createWarehouse(input: { name: string; address?: string; notes?: string }) {
    const companyId = organization.value?.id
    if (!companyId) throw new Error('No organization')
    const { data, error } = await (supabase as any)
      .from('warehouses')
      .insert({ ...input, company_id: companyId })
      .select('id')
      .single()
    if (error) throw error
    await fetchWarehouses()
    return (data as any).id as string
  }

  async function updateWarehouse(id: string, updates: { name?: string; address?: string; notes?: string }) {
    const { error } = await (supabase as any)
      .from('warehouses')
      .update(updates)
      .eq('id', id)
    if (error) throw error
    await fetchWarehouses()
  }

  async function deleteWarehouse(id: string) {
    const { error } = await (supabase as any)
      .from('warehouses')
      .delete()
      .eq('id', id)
    if (error) throw error
    await fetchWarehouses()
  }

  // ── Barcode management ──────────────────────────────────────────────────

  async function fetchBarcodes() {
    const { data, error } = await (supabase as any)
      .from('product_barcodes')
      .select('id, product_id, barcode, format, products(name)')
      .order('barcode')
    if (error) throw error
    barcodes.value = ((data ?? []) as any[]).map((b: any) => ({
      id: b.id,
      product_id: b.product_id,
      barcode: b.barcode,
      format: b.format,
      product_name: b.products?.name ?? null,
    }))
  }

  async function lookupBarcode(barcode: string): Promise<{ product_id: string; product_name: string } | null> {
    const { data, error } = await (supabase as any)
      .from('product_barcodes')
      .select('product_id, products(name)')
      .eq('barcode', barcode)
      .maybeSingle()
    if (error) throw error
    if (!data) return null
    return { product_id: (data as any).product_id, product_name: (data as any).products?.name ?? '' }
  }

  async function addBarcode(input: { product_id: string; barcode: string; format?: string }) {
    const companyId = organization.value?.id
    if (!companyId) throw new Error('No organization')
    const { error } = await (supabase as any)
      .from('product_barcodes')
      .insert({ ...input, format: input.format ?? 'EAN-13', company_id: companyId })
    if (error) throw error
    await fetchBarcodes()
  }

  async function removeBarcode(id: string) {
    const { error } = await (supabase as any)
      .from('product_barcodes')
      .delete()
      .eq('id', id)
    if (error) throw error
    await fetchBarcodes()
  }

  // ── Stock operations ────────────────────────────────────────────────────

  async function fetchBatches(warehouseId: string) {
    loading.value = true
    try {
      const { data, error } = await (supabase as any)
        .from('warehouse_stock_batches')
        .select('id, warehouse_id, product_id, batch_number, expiration_date, quantity, products(name, image_path)')
        .eq('warehouse_id', warehouseId)
        .gt('quantity', 0)
        .order('expiration_date')
      if (error) throw error
      batches.value = ((data ?? []) as any[]).map((b: any) => ({
        id: b.id,
        warehouse_id: b.warehouse_id,
        product_id: b.product_id,
        product_name: b.products?.name ?? null,
        product_image_path: b.products?.image_path ?? null,
        batch_number: b.batch_number,
        expiration_date: b.expiration_date,
        quantity: b.quantity,
      }))
    } finally {
      loading.value = false
    }
  }

  async function fetchProductSummaries(warehouseId: string) {
    loading.value = true
    try {
      // Fetch batches + min stocks in parallel
      const [batchRes, minStockRes] = await Promise.all([
        (supabase as any)
          .from('warehouse_stock_batches')
          .select('product_id, quantity, expiration_date, products(name, image_path)')
          .eq('warehouse_id', warehouseId)
          .gt('quantity', 0),
        (supabase as any)
          .from('product_min_stock')
          .select('product_id, min_quantity')
          .eq('warehouse_id', warehouseId),
      ])

      if (batchRes.error) throw batchRes.error
      if (minStockRes.error) throw minStockRes.error

      const minStockMap = new Map<string, number>()
      for (const ms of (minStockRes.data ?? []) as any[]) {
        minStockMap.set(ms.product_id, ms.min_quantity)
      }

      // Aggregate by product
      const grouped = new Map<string, {
        product_name: string
        product_image_path: string | null
        total_quantity: number
        earliest_expiration: string | null
        batch_count: number
      }>()

      for (const b of (batchRes.data ?? []) as any[]) {
        const pid = b.product_id as string
        const existing = grouped.get(pid)
        if (existing) {
          existing.total_quantity += b.quantity
          existing.batch_count += 1
          if (!existing.earliest_expiration || b.expiration_date < existing.earliest_expiration) {
            existing.earliest_expiration = b.expiration_date
          }
        } else {
          grouped.set(pid, {
            product_name: b.products?.name ?? 'Unknown',
            product_image_path: b.products?.image_path ?? null,
            total_quantity: b.quantity,
            earliest_expiration: b.expiration_date,
            batch_count: 1,
          })
        }
      }

      productSummaries.value = Array.from(grouped.entries()).map(([productId, data]) => {
        const minStock = minStockMap.get(productId) ?? 0
        const expStatus = data.earliest_expiration ? expirationStatus(data.earliest_expiration) : 'ok'
        return {
          product_id: productId,
          product_name: data.product_name,
          product_image_path: data.product_image_path,
          total_quantity: data.total_quantity,
          min_stock: minStock,
          is_below_min: minStock > 0 && data.total_quantity <= minStock,
          earliest_expiration: data.earliest_expiration,
          expiration_status: expStatus,
          batch_count: data.batch_count,
        }
      }).sort((a, b) => a.product_name.localeCompare(b.product_name))
    } finally {
      loading.value = false
    }
  }

  async function bookIncoming(input: {
    warehouse_id: string
    product_id: string
    quantity: number
    expiration_date?: string | null
    batch_number?: string
  }) {
    const companyId = organization.value?.id
    if (!companyId) throw new Error('No organization')

    // Check if batch already exists (same warehouse + product + batch_number + expiration_date)
    let query = (supabase as any)
      .from('warehouse_stock_batches')
      .select('id, quantity')
      .eq('warehouse_id', input.warehouse_id)
      .eq('product_id', input.product_id)
      .eq('batch_number', input.batch_number ?? '')

    if (input.expiration_date) {
      query = query.eq('expiration_date', input.expiration_date)
    } else {
      query = query.is('expiration_date', null)
    }

    const { data: existing } = await query.maybeSingle()

    let batchId: string
    let quantityBefore: number

    if (existing) {
      // Add to existing batch
      batchId = existing.id
      quantityBefore = existing.quantity
      const { error } = await (supabase as any)
        .from('warehouse_stock_batches')
        .update({ quantity: existing.quantity + input.quantity })
        .eq('id', existing.id)
      if (error) throw error
    } else {
      // Create new batch
      quantityBefore = 0
      const { data, error } = await (supabase as any)
        .from('warehouse_stock_batches')
        .insert({
          warehouse_id: input.warehouse_id,
          product_id: input.product_id,
          batch_number: input.batch_number || null,
          expiration_date: input.expiration_date || null,
          quantity: input.quantity,
          company_id: companyId,
        })
        .select('id')
        .single()
      if (error) throw error
      batchId = (data as any).id
    }

    // Log transaction
    const { data: { session } } = await supabase.auth.getSession()
    await (supabase as any).from('warehouse_transactions').insert({
      company_id: companyId,
      warehouse_id: input.warehouse_id,
      product_id: input.product_id,
      batch_id: batchId,
      user_id: session?.user?.id ?? null,
      transaction_type: 'incoming',
      quantity_change: input.quantity,
      quantity_before: quantityBefore,
      quantity_after: quantityBefore + input.quantity,
      batch_number: input.batch_number || null,
      expiration_date: input.expiration_date,
      metadata: {
        _user_email: session?.user?.email ?? null,
      },
    })
  }

  async function adjustStock(input: {
    batch_id: string
    warehouse_id: string
    product_id: string
    quantity_change: number
    reason: 'adjustment_damage' | 'adjustment_expired' | 'adjustment_correction'
    notes?: string
  }) {
    const companyId = organization.value?.id
    if (!companyId) throw new Error('No organization')

    // Get current batch quantity
    const { data: batch, error: fetchErr } = await (supabase as any)
      .from('warehouse_stock_batches')
      .select('quantity, batch_number, expiration_date')
      .eq('id', input.batch_id)
      .single()
    if (fetchErr) throw fetchErr

    const quantityBefore = (batch as any).quantity
    const quantityAfter = Math.max(0, quantityBefore + input.quantity_change)

    const { error } = await (supabase as any)
      .from('warehouse_stock_batches')
      .update({ quantity: quantityAfter })
      .eq('id', input.batch_id)
    if (error) throw error

    // Log transaction
    const { data: { session } } = await supabase.auth.getSession()
    await (supabase as any).from('warehouse_transactions').insert({
      company_id: companyId,
      warehouse_id: input.warehouse_id,
      product_id: input.product_id,
      batch_id: input.batch_id,
      user_id: session?.user?.id ?? null,
      transaction_type: input.reason,
      quantity_change: input.quantity_change,
      quantity_before: quantityBefore,
      quantity_after: quantityAfter,
      batch_number: (batch as any).batch_number,
      expiration_date: (batch as any).expiration_date,
      notes: input.notes ?? null,
      metadata: {
        _user_email: session?.user?.email ?? null,
      },
    })
  }

  async function deductForRefill(input: {
    warehouse_id: string
    product_id: string
    quantity: number
    machine_id: string
  }) {
    const { data: { session } } = await supabase.auth.getSession()
    const { data, error } = await (supabase as any).rpc('deduct_warehouse_stock_fifo', {
      p_warehouse_id: input.warehouse_id,
      p_product_id: input.product_id,
      p_quantity: input.quantity,
      p_user_id: session?.user?.id ?? null,
      p_reference_id: input.machine_id,
      p_notes: 'Machine refill',
      p_metadata: { _user_email: session?.user?.email ?? null },
    })
    if (error) throw error
    return data
  }

  /** Get total available stock for a product across a specific warehouse */
  async function getProductStock(warehouseId: string, productId: string): Promise<number> {
    const { data, error } = await (supabase as any)
      .from('warehouse_stock_batches')
      .select('quantity')
      .eq('warehouse_id', warehouseId)
      .eq('product_id', productId)
      .gt('quantity', 0)
    if (error) throw error
    return ((data ?? []) as any[]).reduce((sum: number, b: any) => sum + b.quantity, 0)
  }

  // ── Min stock thresholds ────────────────────────────────────────────────

  async function fetchMinStocks(warehouseId: string) {
    const { data, error } = await (supabase as any)
      .from('product_min_stock')
      .select('id, product_id, warehouse_id, min_quantity')
      .eq('warehouse_id', warehouseId)
    if (error) throw error
    minStocks.value = (data ?? []) as MinStockEntry[]
  }

  async function setMinStock(input: { product_id: string; warehouse_id: string; min_quantity: number }) {
    const companyId = organization.value?.id
    if (!companyId) throw new Error('No organization')

    const { error } = await (supabase as any)
      .from('product_min_stock')
      .upsert(
        { ...input, company_id: companyId },
        { onConflict: 'product_id,warehouse_id' }
      )
    if (error) throw error
    await fetchMinStocks(input.warehouse_id)
  }

  // ── Transaction history ─────────────────────────────────────────────────

  async function fetchTransactions(warehouseId: string, filters?: { product_id?: string; type?: string; dateFrom?: string; dateTo?: string }) {
    transactionLoading.value = true
    transactionOffset.value = 0
    try {
      let query = (supabase as any)
        .from('warehouse_transactions')
        .select('id, created_at, warehouse_id, product_id, batch_id, user_id, transaction_type, quantity_change, quantity_before, quantity_after, batch_number, expiration_date, reference_id, notes, metadata, products(name)')
        .eq('warehouse_id', warehouseId)
        .order('created_at', { ascending: false })
        .range(0, PAGE_SIZE - 1)

      if (filters?.product_id) query = query.eq('product_id', filters.product_id)
      if (filters?.type) query = query.eq('transaction_type', filters.type)
      if (filters?.dateFrom) query = query.gte('created_at', filters.dateFrom)
      if (filters?.dateTo) query = query.lte('created_at', filters.dateTo + 'T23:59:59')

      const { data, error } = await query
      if (error) throw error

      transactions.value = ((data ?? []) as any[]).map(mapTransaction)
      transactionHasMore.value = (data ?? []).length >= PAGE_SIZE
      transactionOffset.value = (data ?? []).length
    } finally {
      transactionLoading.value = false
    }
  }

  async function fetchMoreTransactions(warehouseId: string, filters?: { product_id?: string; type?: string; dateFrom?: string; dateTo?: string }) {
    transactionLoading.value = true
    try {
      let query = (supabase as any)
        .from('warehouse_transactions')
        .select('id, created_at, warehouse_id, product_id, batch_id, user_id, transaction_type, quantity_change, quantity_before, quantity_after, batch_number, expiration_date, reference_id, notes, metadata, products(name)')
        .eq('warehouse_id', warehouseId)
        .order('created_at', { ascending: false })
        .range(transactionOffset.value, transactionOffset.value + PAGE_SIZE - 1)

      if (filters?.product_id) query = query.eq('product_id', filters.product_id)
      if (filters?.type) query = query.eq('transaction_type', filters.type)
      if (filters?.dateFrom) query = query.gte('created_at', filters.dateFrom)
      if (filters?.dateTo) query = query.lte('created_at', filters.dateTo + 'T23:59:59')

      const { data, error } = await query
      if (error) throw error

      const mapped = ((data ?? []) as any[]).map(mapTransaction)
      transactions.value = [...transactions.value, ...mapped]
      transactionHasMore.value = (data ?? []).length >= PAGE_SIZE
      transactionOffset.value += (data ?? []).length
    } finally {
      transactionLoading.value = false
    }
  }

  function mapTransaction(t: any): WarehouseTransaction {
    return {
      id: t.id,
      created_at: t.created_at,
      warehouse_id: t.warehouse_id,
      product_id: t.product_id,
      product_name: t.products?.name ?? null,
      batch_id: t.batch_id,
      user_id: t.user_id,
      transaction_type: t.transaction_type,
      quantity_change: t.quantity_change,
      quantity_before: t.quantity_before,
      quantity_after: t.quantity_after,
      batch_number: t.batch_number,
      expiration_date: t.expiration_date,
      reference_id: t.reference_id,
      notes: t.notes,
      metadata: t.metadata,
    }
  }

  // ── Realtime ────────────────────────────────────────────────────────────

  function subscribeToStockUpdates(warehouseId: string) {
    const channel = supabase
      .channel(`warehouse-stock-${warehouseId}`)
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'warehouse_stock_batches', filter: `warehouse_id=eq.${warehouseId}` },
        () => {
          fetchBatches(warehouseId)
          fetchProductSummaries(warehouseId)
        }
      )
      .subscribe()

    return () => supabase.removeChannel(channel)
  }

  // ── Transaction type helpers ────────────────────────────────────────────

  function transactionTypeLabel(type: string): string {
    switch (type) {
      case 'incoming': return 'Incoming'
      case 'outgoing_refill': return 'Refill'
      case 'adjustment_damage': return 'Damaged'
      case 'adjustment_expired': return 'Expired'
      case 'adjustment_correction': return 'Correction'
      case 'transfer_out': return 'Transfer out'
      case 'transfer_in': return 'Transfer in'
      default: return type
    }
  }

  function transactionTypeBadgeClass(type: string): string {
    switch (type) {
      case 'incoming': return 'bg-green-100 text-green-700 dark:bg-green-950/40 dark:text-green-400'
      case 'outgoing_refill': return 'bg-blue-100 text-blue-700 dark:bg-blue-950/40 dark:text-blue-400'
      case 'adjustment_damage': return 'bg-red-100 text-red-700 dark:bg-red-950/40 dark:text-red-400'
      case 'adjustment_expired': return 'bg-red-100 text-red-700 dark:bg-red-950/40 dark:text-red-400'
      case 'adjustment_correction': return 'bg-amber-100 text-amber-700 dark:bg-amber-950/40 dark:text-amber-400'
      case 'transfer_out': return 'bg-purple-100 text-purple-700 dark:bg-purple-950/40 dark:text-purple-400'
      case 'transfer_in': return 'bg-purple-100 text-purple-700 dark:bg-purple-950/40 dark:text-purple-400'
      default: return 'bg-muted text-muted-foreground'
    }
  }

  return {
    warehouses, batches, transactions, productSummaries, barcodes, minStocks,
    loading, transactionLoading, transactionHasMore,
    fetchWarehouses, createWarehouse, updateWarehouse, deleteWarehouse,
    fetchBarcodes, lookupBarcode, addBarcode, removeBarcode,
    fetchBatches, fetchProductSummaries, bookIncoming, adjustStock, deductForRefill, getProductStock,
    fetchMinStocks, setMinStock,
    fetchTransactions, fetchMoreTransactions,
    subscribeToStockUpdates,
    transactionTypeLabel, transactionTypeBadgeClass,
  }
}
