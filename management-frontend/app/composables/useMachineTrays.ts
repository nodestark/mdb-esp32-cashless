import { useSupabaseClient } from '#imports'

interface Tray {
  id: string
  machine_id: string
  item_number: number
  product_id: string | null
  product_name: string | null
  capacity: number
  current_stock: number
  min_stock: number
}

export function useMachineTrays() {
  const trays = ref<Tray[]>([])
  const loading = ref(false)

  async function fetchTrays(machineId: string, { silent = false } = {}) {
    if (!silent) loading.value = true
    try {
      const supabase = useSupabaseClient()
      const { data, error } = await supabase
        .from('machine_trays')
        .select('id, machine_id, item_number, product_id, capacity, current_stock, min_stock, products(name)')
        .eq('machine_id', machineId)
        .order('item_number')

      if (error) throw error

      trays.value = ((data ?? []) as any[]).map((t) => ({
        id: t.id,
        machine_id: t.machine_id,
        item_number: t.item_number,
        product_id: t.product_id,
        product_name: t.products?.name ?? null,
        capacity: t.capacity,
        current_stock: t.current_stock,
        min_stock: t.min_stock ?? 0,
      }))
    } finally {
      if (!silent) loading.value = false
    }
  }

  async function upsertTray(tray: { machine_id: string; item_number: number; product_id: string | null; capacity: number; current_stock: number }) {
    const supabase = useSupabaseClient()
    const { error } = await supabase
      .from('machine_trays')
      .upsert(tray, { onConflict: 'machine_id,item_number' })

    if (error) throw error
    await fetchTrays(tray.machine_id, { silent: true })
  }

  async function refillTray(trayId: string, machineId: string, newStock: number) {
    const supabase = useSupabaseClient()
    const { error } = await supabase
      .from('machine_trays')
      .update({ current_stock: newStock })
      .eq('id', trayId)

    if (error) throw error
    await fetchTrays(machineId, { silent: true })
  }

  async function updateTray(trayId: string, machineId: string, updates: { item_number?: number; product_id?: string | null; capacity?: number; current_stock?: number; min_stock?: number }) {
    const supabase = useSupabaseClient()
    const { error } = await supabase
      .from('machine_trays')
      .update(updates)
      .eq('id', trayId)

    if (error) throw error
    await fetchTrays(machineId, { silent: true })
  }

  /** Set a single tray's stock to its capacity (one-click "Full") */
  async function refillToFull(trayId: string, machineId: string) {
    const tray = trays.value.find(t => t.id === trayId)
    if (!tray) return
    // Optimistically update UI immediately
    tray.current_stock = tray.capacity
    await refillTray(trayId, machineId, tray.capacity)
  }

  /** Set all below-minimum trays to their capacity */
  async function refillAll(machineId: string) {
    const supabase = useSupabaseClient()
    const lowTrays = trays.value.filter(t => t.min_stock > 0 && t.current_stock <= t.min_stock)
    if (lowTrays.length === 0) return

    // Optimistically update UI
    for (const t of lowTrays) t.current_stock = t.capacity

    // Parallel DB updates
    const results = await Promise.all(lowTrays.map(tray =>
      supabase
        .from('machine_trays')
        .update({ current_stock: tray.capacity })
        .eq('id', tray.id)
    ))

    const hasError = results.some(r => r.error)
    if (hasError) {
      // Re-fetch to get accurate state
      await fetchTrays(machineId, { silent: true })
      throw new Error('Some trays failed to update')
    }
    await fetchTrays(machineId, { silent: true })
  }

  async function batchCreateTrays(machineId: string, startSlot: number, count: number, capacity: number) {
    const supabase = useSupabaseClient()
    const rows = Array.from({ length: count }, (_, i) => ({
      machine_id: machineId,
      item_number: startSlot + i,
      capacity,
      current_stock: 0,
    }))
    const { error } = await supabase
      .from('machine_trays')
      .upsert(rows, { onConflict: 'machine_id,item_number', ignoreDuplicates: true })

    if (error) throw error
    await fetchTrays(machineId, { silent: true })
  }

  async function deleteTray(trayId: string, machineId: string) {
    const supabase = useSupabaseClient()
    const { error } = await supabase
      .from('machine_trays')
      .delete()
      .eq('id', trayId)

    if (error) throw error
    await fetchTrays(machineId, { silent: true })
  }

  function subscribeToTrayUpdates(machineId: string) {
    const supabase = useSupabaseClient()
    const channel = supabase
      .channel(`trays-realtime-${machineId}`)
      .on(
        'postgres_changes',
        {
          event: 'UPDATE',
          schema: 'public',
          table: 'machine_trays',
          filter: `machine_id=eq.${machineId}`,
        },
        (payload) => {
          const updated = payload.new as Record<string, any>
          const tray = trays.value.find(t => t.id === updated.id)
          if (tray) {
            tray.current_stock = updated.current_stock
            tray.capacity = updated.capacity
            tray.product_id = updated.product_id
            tray.min_stock = updated.min_stock ?? 0
          }
        }
      )
      .on(
        'postgres_changes',
        {
          event: 'INSERT',
          schema: 'public',
          table: 'machine_trays',
          filter: `machine_id=eq.${machineId}`,
        },
        () => {
          fetchTrays(machineId)
        }
      )
      .on(
        'postgres_changes',
        {
          event: 'DELETE',
          schema: 'public',
          table: 'machine_trays',
          filter: `machine_id=eq.${machineId}`,
        },
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
