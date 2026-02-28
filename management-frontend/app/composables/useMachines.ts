import { useSupabaseClient } from '#imports'

interface Embedded {
  id: string
  status: string
  status_at: string
  subdomain: number
  mac_address?: string
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
          embeddeds(id, status, status_at, subdomain)
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
      const [todaySalesRes, yesterdaySalesRes, paxRes, ...lastSaleResults] = await Promise.all([
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
    } finally {
      loading.value = false
    }
  }

  async function fetchUnassignedEmbeddeds() {
    const supabase = useSupabaseClient()

    const [allRes, assignedRes] = await Promise.all([
      supabase
        .from('embeddeds')
        .select('id, mac_address, subdomain, status, status_at'),
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
      .subscribe((status, err) => {
        console.log('[realtime] machines channel status:', status)
        if (err) console.error('[realtime] machines channel error:', err)
      })

    return () => supabase.removeChannel(channel)
  }

  return { machines, loading, fetchMachines, fetchUnassignedEmbeddeds, swapDevice, subscribeToStatusUpdates }
}
