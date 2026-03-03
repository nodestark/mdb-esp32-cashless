import { useSupabaseClient } from '#imports'

interface Embedded {
  id: string
  status: string
  status_at: string
  subdomain: number
  mac_address?: string
  firmware_version?: string
  firmware_build_date?: string
}

interface VendingMachine {
  id: string
  name: string
  location_lat: number | null
  location_lon: number | null
  embedded: string | null
  embeddeds: Embedded | null
  last_sale_at?: string | null
  last_sale_amount?: number | null
  last_sale_item_number?: number | null
  today_revenue?: number
  today_sales_count?: number
  yesterday_revenue?: number
  paxcounter_count?: number | null
  // Stock fields
  total_trays?: number
  low_trays?: number
  empty_trays?: number
  stock_health?: 'ok' | 'low' | 'critical'
  stock_percent?: number
  tray_summary?: { product_name: string; product_id: string | null; deficit: number; image_path: string | null }[]
}

interface PendingToken {
  id: string
  short_code: string
  name: string | null
  created_at: string
  expires_at: string
  device_only: boolean
}

export function useMachines() {
  const machines = useState<VendingMachine[]>('machines', () => [])
  const loading = ref(false)

  async function fetchMachines() {
    loading.value = true
    try {
      const supabase = useSupabaseClient()
      const { data, error } = await supabase
        .from('vendingMachine')
        .select(`
          id, name, location_lat, location_lon, embedded,
          embeddeds(id, status, status_at, subdomain, mac_address, firmware_version, firmware_build_date)
        `)

      if (error) throw error
      machines.value = (data ?? []) as VendingMachine[]

      const machineIds = machines.value.map(m => m.id)

      if (machineIds.length === 0) {
        loading.value = false
        return
      }

      // Date boundaries
      const now = new Date()
      const todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).toISOString()
      const yesterdayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 1).toISOString()

      // Batch queries in parallel — use machine_id instead of embedded_id
      const [todaySalesRes, yesterdaySalesRes, paxRes, traysRes, ...lastSaleResults] = await Promise.all([
        // Today's sales
        supabase
          .from('sales')
          .select('machine_id, item_price')
          .in('machine_id', machineIds)
          .gte('created_at', todayStart),
        // Yesterday's sales
        supabase
          .from('sales')
          .select('machine_id, item_price')
          .in('machine_id', machineIds)
          .gte('created_at', yesterdayStart)
          .lt('created_at', todayStart),
        // Latest paxcounter per machine
        supabase
          .from('paxcounter')
          .select('machine_id, count')
          .in('machine_id', machineIds)
          .order('created_at', { ascending: false }),
        // All tray data in one batch (with product names)
        supabase
          .from('machine_trays')
          .select('machine_id, item_number, product_id, capacity, current_stock, min_stock, fill_when_below, products(name, image_path)')
          .in('machine_id', machineIds),
        // Last sale per machine
        ...machines.value.map(m =>
          supabase
            .from('sales')
            .select('created_at, item_price, item_number')
            .eq('machine_id', m.id)
            .order('created_at', { ascending: false })
            .limit(1)
            .maybeSingle()
        ),
      ])

      // Aggregate today's sales per machine
      const todayMap = new Map<string, { revenue: number; count: number }>()
      const todayRows = (todaySalesRes.data ?? []) as { machine_id: string; item_price: number }[]
      for (const row of todayRows) {
        if (!row.machine_id) continue
        const entry = todayMap.get(row.machine_id) ?? { revenue: 0, count: 0 }
        entry.revenue += row.item_price ?? 0
        entry.count += 1
        todayMap.set(row.machine_id, entry)
      }

      // Aggregate yesterday's sales per machine
      const yesterdayMap = new Map<string, number>()
      const yesterdayRows = (yesterdaySalesRes.data ?? []) as { machine_id: string; item_price: number }[]
      for (const row of yesterdayRows) {
        if (!row.machine_id) continue
        yesterdayMap.set(row.machine_id, (yesterdayMap.get(row.machine_id) ?? 0) + (row.item_price ?? 0))
      }

      // Dedupe paxcounter to latest per machine
      const paxMap = new Map<string, number>()
      const paxRows = (paxRes.data ?? []) as { machine_id: string; count: number }[]
      for (const row of paxRows) {
        if (!row.machine_id) continue
        if (!paxMap.has(row.machine_id)) {
          paxMap.set(row.machine_id, row.count)
        }
      }

      // Apply last sale results
      for (let i = 0; i < machines.value.length; i++) {
        const machine = machines.value[i]!
        const saleData = lastSaleResults[i]?.data as { created_at: string; item_price: number; item_number: number } | null
        machine.last_sale_at = saleData?.created_at ?? null
        machine.last_sale_amount = saleData?.item_price ?? null
        machine.last_sale_item_number = saleData?.item_number ?? null
      }

      // Apply aggregated data to machines
      for (const machine of machines.value) {
        const todayStats = todayMap.get(machine.id)
        machine.today_revenue = todayStats?.revenue ?? 0
        machine.today_sales_count = todayStats?.count ?? 0
        machine.yesterday_revenue = yesterdayMap.get(machine.id) ?? 0
        machine.paxcounter_count = paxMap.get(machine.id) ?? null
      }

      // Aggregate stock stats per machine
      const trayRows = (traysRes.data ?? []) as {
        machine_id: string
        item_number: number
        product_id: string | null
        capacity: number
        current_stock: number
        min_stock: number
        fill_when_below: number
        products: { name: string; image_path: string | null } | null
      }[]

      const stockMap = new Map<string, {
        total: number
        low: number
        empty: number
        totalStock: number
        totalCapacity: number
        deficits: Map<string, { product_name: string; product_id: string | null; deficit: number; image_path: string | null }>
        fillBelowPending: { product_id: string | null; capacity: number; current_stock: number; item_number: number; products: { name: string; image_path: string | null } | null }[]
      }>()

      // Pass 1: count low/empty trays and collect fill_when_below candidates
      for (const tray of trayRows) {
        if (!tray.machine_id) continue
        let entry = stockMap.get(tray.machine_id)
        if (!entry) {
          entry = { total: 0, low: 0, empty: 0, totalStock: 0, totalCapacity: 0, deficits: new Map(), fillBelowPending: [] }
          stockMap.set(tray.machine_id, entry)
        }
        entry.total++
        entry.totalStock += tray.current_stock
        entry.totalCapacity += tray.capacity

        const isLow = tray.min_stock > 0 && tray.current_stock <= tray.min_stock
        const isEmpty = tray.current_stock === 0
        const isFillBelow = !isLow && !isEmpty && tray.fill_when_below > 0 && tray.current_stock <= tray.fill_when_below

        if (isEmpty) entry.empty++
        else if (isLow) entry.low++

        if (isLow || isEmpty) {
          const deficit = tray.capacity - tray.current_stock
          const productName = tray.products?.name ?? `Slot ${tray.item_number}`
          const imagePath = tray.products?.image_path ?? null
          const key = tray.product_id ?? `slot-${tray.item_number}`
          const existing = entry.deficits.get(key)
          if (existing) {
            existing.deficit += deficit
          } else {
            entry.deficits.set(key, { product_name: productName, product_id: tray.product_id, deficit, image_path: imagePath })
          }
        }

        if (isFillBelow) {
          entry.fillBelowPending.push(tray)
        }
      }

      // Pass 2: for machines with critical/low trays, add fill_when_below deficits to packing list
      for (const [, entry] of stockMap) {
        if (entry.low + entry.empty === 0) continue
        for (const tray of entry.fillBelowPending) {
          const deficit = tray.capacity - tray.current_stock
          if (deficit <= 0) continue
          const productName = tray.products?.name ?? `Slot ${tray.item_number}`
          const imagePath = tray.products?.image_path ?? null
          const key = tray.product_id ?? `slot-${tray.item_number}`
          const existing = entry.deficits.get(key)
          if (existing) {
            existing.deficit += deficit
          } else {
            entry.deficits.set(key, { product_name: productName, product_id: tray.product_id, deficit, image_path: imagePath })
          }
        }
      }

      // Apply stock stats to machines
      for (const machine of machines.value) {
        const stock = stockMap.get(machine.id)
        if (stock) {
          machine.total_trays = stock.total
          machine.low_trays = stock.low + stock.empty
          machine.empty_trays = stock.empty
          machine.stock_health = stock.empty > 0 ? 'critical' : (stock.low > 0 ? 'low' : 'ok')
          machine.stock_percent = stock.totalCapacity > 0
            ? Math.round((stock.totalStock / stock.totalCapacity) * 100)
            : 0
          machine.tray_summary = Array.from(stock.deficits.values()).sort((a, b) => b.deficit - a.deficit)
        } else {
          machine.total_trays = 0
          machine.low_trays = 0
          machine.empty_trays = 0
          machine.stock_health = 'ok'
          machine.stock_percent = 0
          machine.tray_summary = []
        }
      }

      // Sort machines by stock urgency: critical > low > ok, then by low_trays desc
      const healthOrder: Record<string, number> = { critical: 0, low: 1, ok: 2 }
      machines.value.sort((a, b) => {
        const ha = healthOrder[a.stock_health ?? 'ok']
        const hb = healthOrder[b.stock_health ?? 'ok']
        if (ha !== hb) return ha - hb
        return (b.low_trays ?? 0) - (a.low_trays ?? 0)
      })
    } finally {
      loading.value = false
    }
  }

  async function fetchUnassignedEmbeddeds() {
    const supabase = useSupabaseClient()

    const [allRes, assignedRes] = await Promise.all([
      supabase
        .from('embeddeds')
        .select('id, mac_address, subdomain, status, status_at, firmware_version, firmware_build_date'),
      supabase
        .from('vendingMachine')
        .select('embedded')
        .not('embedded', 'is', null),
    ])

    const assignedIds = new Set(
      (assignedRes.data ?? []).map((m: any) => m.embedded).filter(Boolean)
    )

    return ((allRes.data ?? []) as Embedded[]).filter(e => !assignedIds.has(e.id))
  }

  async function swapDevice(machineId: string, newEmbeddedId: string | null) {
    const supabase = useSupabaseClient()

    if (newEmbeddedId) {
      // Unassign this device from any other machine first
      await supabase
        .from('vendingMachine')
        .update({ embedded: null } as any)
        .eq('embedded', newEmbeddedId)
    }

    // Assign to target machine (or detach if null)
    const { error } = await supabase
      .from('vendingMachine')
      .update({ embedded: newEmbeddedId } as any)
      .eq('id', machineId)

    if (error) throw error
    await fetchMachines()
  }

  function subscribeToStatusUpdates() {
    const supabase = useSupabaseClient()
    const channel = supabase
      .channel('machines-realtime')
      .on(
        'postgres_changes',
        { event: 'UPDATE', schema: 'public', table: 'embeddeds' },
        (payload) => {
          const updated = payload.new as Embedded
          const machine = machines.value.find(m => m.embeddeds?.id === updated.id)
          if (machine && machine.embeddeds) {
            machine.embeddeds.status = updated.status
            machine.embeddeds.status_at = updated.status_at
            if (updated.firmware_version) {
              machine.embeddeds.firmware_version = updated.firmware_version
            }
            if (updated.firmware_build_date) {
              machine.embeddeds.firmware_build_date = updated.firmware_build_date
            }
          }
        }
      )
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'vendingMachine' },
        () => {
          fetchMachines()
        }
      )
      .on(
        'postgres_changes',
        { event: 'UPDATE', schema: 'public', table: 'vendingMachine' },
        (payload) => {
          const updated = payload.new as Record<string, any>
          const machine = machines.value.find(m => m.id === updated.id)
          if (machine) {
            machine.name = updated.name
            machine.location_lat = updated.location_lat
            machine.location_lon = updated.location_lon
            // If embedded link changed, re-fetch to get the joined data
            if (machine.embedded !== updated.embedded) {
              fetchMachines()
            }
          }
        }
      )
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'sales' },
        (payload) => {
          const sale = payload.new as Record<string, any>
          const machine = machines.value.find(m => m.id === sale.machine_id)
          if (machine) {
            machine.last_sale_at = sale.created_at
            machine.last_sale_amount = sale.item_price
            machine.last_sale_item_number = sale.item_number ?? null

            // Update today's stats if the sale is from today
            const saleDate = new Date(sale.created_at)
            const now = new Date()
            if (
              saleDate.getFullYear() === now.getFullYear() &&
              saleDate.getMonth() === now.getMonth() &&
              saleDate.getDate() === now.getDate()
            ) {
              machine.today_revenue = (machine.today_revenue ?? 0) + (sale.item_price ?? 0)
              machine.today_sales_count = (machine.today_sales_count ?? 0) + 1
            }
          }
        }
      )
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'machine_trays' },
        () => {
          fetchMachines()
        }
      )
      .subscribe((_status, err) => {
        if (err) console.error('[realtime] machines channel error:', err)
      })

    return () => supabase.removeChannel(channel)
  }

  async function createMachine(name: string, companyId: string) {
    const supabase = useSupabaseClient()
    const { error } = await supabase
      .from('vendingMachine')
      .insert({ name, company: companyId } as any)
    if (error) throw error
    await fetchMachines()
  }

  const pendingTokens = useState<PendingToken[]>('pending-tokens', () => [])

  async function fetchPendingTokens() {
    const supabase = useSupabaseClient()
    const { data, error } = await supabase
      .from('device_provisioning')
      .select('id, short_code, name, created_at, expires_at, device_only')
      .is('used_at', null)
      .order('created_at', { ascending: false })
    if (error) throw error
    pendingTokens.value = (data ?? []) as PendingToken[]
  }

  async function deletePendingToken(id: string) {
    const supabase = useSupabaseClient()
    const { error } = await supabase
      .from('device_provisioning')
      .delete()
      .eq('id', id)
    if (error) throw error
    await fetchPendingTokens()
  }

  return {
    machines, loading, fetchMachines, fetchUnassignedEmbeddeds, swapDevice, subscribeToStatusUpdates,
    createMachine, pendingTokens, fetchPendingTokens, deletePendingToken,
  }
}
