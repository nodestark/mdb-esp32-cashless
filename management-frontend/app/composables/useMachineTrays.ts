import { useSupabaseClient, useSupabaseUser } from '#imports'
import { useOrganization } from './useOrganization'

interface Tray {
  id: string
  machine_id: string
  item_number: number
  product_id: string | null
  product_name: string | null
  capacity: number
  current_stock: number
  min_stock: number
  fill_when_below: number
}

export function useMachineTrays() {
  const trays = ref<Tray[]>([])
  const loading = ref(false)
  const supabase = useSupabaseClient()
  const user = useSupabaseUser()
  const { organization } = useOrganization()

  // Cache machine name so we don't look it up on every stock change
  const machineNameCache = new Map<string, string>()

  async function getMachineName(machineId: string): Promise<string | null> {
    if (machineNameCache.has(machineId)) return machineNameCache.get(machineId) ?? null
    const { data } = await (supabase as any).from('vendingMachine').select('name').eq('id', machineId).maybeSingle()
    const name = data?.name ?? null
    if (name) machineNameCache.set(machineId, name)
    return name
  }

  async function fetchTrays(machineId: string, { silent = false } = {}) {
    if (!silent) loading.value = true
    try {
      const { data, error } = await (supabase as any)
        .from('machine_trays')
        .select('id, machine_id, item_number, product_id, capacity, current_stock, min_stock, fill_when_below, products(name)')
        .eq('machine_id', machineId)
        .order('item_number')

      if (error) throw error

      trays.value = ((data ?? []) as any[]).map((t: any) => ({
        id: t.id,
        machine_id: t.machine_id,
        item_number: t.item_number,
        product_id: t.product_id,
        product_name: t.products?.name ?? null,
        capacity: t.capacity,
        current_stock: t.current_stock,
        min_stock: t.min_stock ?? 0,
        fill_when_below: t.fill_when_below ?? 0,
      }))
    } finally {
      if (!silent) loading.value = false
    }
  }

  // ── Activity log helper ───────────────────────────────────────────────────
  async function logActivity(action: string, entityId: string | null, metadata: Record<string, unknown>) {
    try {
      // Read user from the Supabase auth session directly — useSupabaseUser()
      // is a Vue ref that may not be hydrated yet, but the client session is
      // always available when authenticated.
      const { data: { session } } = await supabase.auth.getSession()
      const u = session?.user ?? null
      const fullName = [u?.user_metadata?.first_name, u?.user_metadata?.last_name]
        .filter(Boolean).join(' ').trim()
      const userDisplay = fullName || u?.email || null

      await (supabase as any).from('activity_log').insert({
        company_id: organization.value?.id,
        user_id: u?.id ?? null,
        entity_type: 'stock',
        entity_id: entityId,
        action,
        metadata: {
          ...metadata,
          _user_email: u?.email ?? null,
          _user_display: userDisplay,
        },
      })
    } catch (err) {
      console.warn('activity_log insert failed:', err)
    }
  }

  async function upsertTray(tray: { machine_id: string; item_number: number; product_id: string | null; capacity: number; current_stock: number }) {
    const { error } = await (supabase as any)
      .from('machine_trays')
      .upsert(tray, { onConflict: 'machine_id,item_number' })

    if (error) throw error
    await fetchTrays(tray.machine_id, { silent: true })
  }

  async function refillTray(trayId: string, machineId: string, newStock: number) {
    const tray = trays.value.find(t => t.id === trayId)
    const oldStock = tray?.current_stock ?? 0

    const { error } = await (supabase as any)
      .from('machine_trays')
      .update({ current_stock: newStock })
      .eq('id', trayId)

    if (error) throw error
    await fetchTrays(machineId, { silent: true })

    const machineName = await getMachineName(machineId)
    await logActivity('stock_updated', trayId, {
      machine_id: machineId,
      machine_name: machineName,
      item_number: tray?.item_number ?? null,
      product_name: tray?.product_name ?? null,
      old_stock: oldStock,
      new_stock: newStock,
      capacity: tray?.capacity ?? null,
    })
  }

  async function updateTray(trayId: string, machineId: string, updates: { item_number?: number; product_id?: string | null; capacity?: number; current_stock?: number; min_stock?: number; fill_when_below?: number }) {
    const tray = trays.value.find(t => t.id === trayId)
    const { error } = await (supabase as any)
      .from('machine_trays')
      .update(updates)
      .eq('id', trayId)

    if (error) throw error
    await fetchTrays(machineId, { silent: true })

    // Only log if a stock-related field changed
    if (updates.current_stock !== undefined || updates.min_stock !== undefined || updates.capacity !== undefined || updates.fill_when_below !== undefined) {
      const machineName = await getMachineName(machineId)
      await logActivity('stock_updated', trayId, {
        machine_id: machineId,
        machine_name: machineName,
        item_number: tray?.item_number ?? null,
        product_name: tray?.product_name ?? null,
        old_stock: updates.current_stock !== undefined ? tray?.current_stock ?? null : undefined,
        new_stock: updates.current_stock,
        old_min_stock: updates.min_stock !== undefined ? tray?.min_stock ?? null : undefined,
        new_min_stock: updates.min_stock,
        old_capacity: updates.capacity !== undefined ? tray?.capacity ?? null : undefined,
        new_capacity: updates.capacity,
        old_fill_when_below: updates.fill_when_below !== undefined ? tray?.fill_when_below ?? null : undefined,
        new_fill_when_below: updates.fill_when_below,
      })
    }
  }

  /** Set a single tray's stock to its capacity (one-click "Full") */
  async function refillToFull(trayId: string, machineId: string) {
    const tray = trays.value.find(t => t.id === trayId)
    if (!tray) return
    tray.current_stock = tray.capacity
    await refillTray(trayId, machineId, tray.capacity)
  }

  /**
   * Set all below-minimum and below-fill-threshold trays to their capacity.
   * If packedQuantities is provided (product_id → packed amount), only refill
   * by the packed amount instead of to full capacity. This respects warehouse
   * stock limits when coming from the packing list flow.
   */
  async function refillAll(machineId: string, packedQuantities?: Record<string, number>) {
    const hasCritical = trays.value.some(t => t.min_stock > 0 && t.current_stock <= t.min_stock)
    if (!hasCritical) return

    const refillTargets = trays.value.filter(t => {
      const isBelowMin = t.min_stock > 0 && t.current_stock <= t.min_stock
      const isBelowFill = t.fill_when_below > 0 && t.current_stock <= t.fill_when_below
      return isBelowMin || isBelowFill
    })
    if (refillTargets.length === 0) return

    // Calculate target stock per tray
    // When packedQuantities is provided, distribute packed amounts across trays
    // (emptiest trays first to prioritize critical refills)
    const sorted = [...refillTargets].sort((a, b) => a.current_stock - b.current_stock)
    const remaining = packedQuantities ? { ...packedQuantities } : null

    const updates: { tray: typeof sorted[0]; newStock: number }[] = []
    for (const tray of sorted) {
      let newStock: number
      if (remaining && tray.product_id) {
        const available = remaining[tray.product_id] ?? 0
        if (available <= 0) continue // nothing packed for this product
        const deficit = tray.capacity - tray.current_stock
        const addAmount = Math.min(deficit, available)
        remaining[tray.product_id] -= addAmount
        newStock = tray.current_stock + addAmount
      } else {
        newStock = tray.capacity
      }
      if (newStock > tray.current_stock) {
        updates.push({ tray, newStock })
      }
    }
    if (updates.length === 0) return

    // Snapshot before optimistic update
    const snapshot = updates.map(u => ({ id: u.tray.id, item_number: u.tray.item_number, product_name: u.tray.product_name, product_id: u.tray.product_id, old_stock: u.tray.current_stock, new_stock: u.newStock }))

    // Optimistic update
    for (const u of updates) u.tray.current_stock = u.newStock

    const results = await Promise.all(updates.map(u =>
      (supabase as any)
        .from('machine_trays')
        .update({ current_stock: u.newStock })
        .eq('id', u.tray.id)
    ))

    const hasError = results.some((r: any) => r.error)
    if (hasError) {
      await fetchTrays(machineId, { silent: true })
      throw new Error('Some trays failed to update')
    }
    await fetchTrays(machineId, { silent: true })

    const machineName = await getMachineName(machineId)
    await logActivity('stock_refill_all', machineId, {
      machine_id: machineId,
      machine_name: machineName,
      trays_refilled: snapshot,
    })
  }

  async function batchCreateTrays(machineId: string, startSlot: number, count: number, capacity: number) {
    const rows = Array.from({ length: count }, (_, i) => ({
      machine_id: machineId,
      item_number: startSlot + i,
      capacity,
      current_stock: 0,
    }))
    const { error } = await (supabase as any)
      .from('machine_trays')
      .upsert(rows, { onConflict: 'machine_id,item_number', ignoreDuplicates: true })

    if (error) throw error
    await fetchTrays(machineId, { silent: true })
  }

  async function deleteTray(trayId: string, machineId: string) {
    const { error } = await (supabase as any)
      .from('machine_trays')
      .delete()
      .eq('id', trayId)

    if (error) throw error
    await fetchTrays(machineId, { silent: true })
  }

  function subscribeToTrayUpdates(machineId: string) {
    const channel = supabase
      .channel(`trays-realtime-${machineId}`)
      .on(
        'postgres_changes',
        { event: 'UPDATE', schema: 'public', table: 'machine_trays', filter: `machine_id=eq.${machineId}` },
        (payload) => {
          const updated = payload.new as Record<string, any>
          const tray = trays.value.find(t => t.id === updated.id)
          if (tray) {
            tray.current_stock = updated.current_stock
            tray.capacity = updated.capacity
            tray.product_id = updated.product_id
            tray.min_stock = updated.min_stock ?? 0
            tray.fill_when_below = updated.fill_when_below ?? 0
          }
        }
      )
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'machine_trays', filter: `machine_id=eq.${machineId}` },
        () => fetchTrays(machineId)
      )
      .on(
        'postgres_changes',
        { event: 'DELETE', schema: 'public', table: 'machine_trays', filter: `machine_id=eq.${machineId}` },
        (payload) => {
          const deleted = payload.old as Record<string, any>
          trays.value = trays.value.filter(t => t.id !== deleted.id)
        }
      )
      .subscribe()

    return () => supabase.removeChannel(channel)
  }

  return { trays, loading, fetchTrays, upsertTray, updateTray, batchCreateTrays, refillTray, refillToFull, refillAll, deleteTray, subscribeToTrayUpdates }
}
