import { useSupabaseClient } from '#imports'

interface Embedded {
  id: string
  status: string
  status_at: string
  subdomain: number
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

      // Collect embedded IDs for batch queries
      const ids = machines.value
        .map(m => m.embeddeds?.id)
        .filter((id): id is string => !!id)

      if (ids.length === 0) {
        loading.value = false
        return
      }

      // Date boundaries
      const now = new Date()
      const todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).toISOString()
      const yesterdayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 1).toISOString()

      // Batch queries in parallel
      const [todaySalesRes, yesterdaySalesRes, paxRes, ...lastSaleResults] = await Promise.all([
        // Today's sales
        supabase
          .from('sales')
          .select('embedded_id, item_price')
          .in('embedded_id', ids)
          .gte('created_at', todayStart),
        // Yesterday's sales
        supabase
          .from('sales')
          .select('embedded_id, item_price')
          .in('embedded_id', ids)
          .gte('created_at', yesterdayStart)
          .lt('created_at', todayStart),
        // Latest paxcounter per device
        supabase
          .from('paxcounter')
          .select('embedded_id, count')
          .in('embedded_id', ids)
          .order('created_at', { ascending: false }),
        // Last sale per machine
        ...machines.value
          .filter(m => m.embeddeds?.id)
          .map(m =>
            supabase
              .from('sales')
              .select('created_at, item_price, item_number')
              .eq('embedded_id', m.embeddeds!.id)
              .order('created_at', { ascending: false })
              .limit(1)
              .maybeSingle()
          ),
      ])

      // Aggregate today's sales per device
      const todayMap = new Map<string, { revenue: number; count: number }>()
      const todayRows = (todaySalesRes.data ?? []) as { embedded_id: string; item_price: number }[]
      for (const row of todayRows) {
        const entry = todayMap.get(row.embedded_id) ?? { revenue: 0, count: 0 }
        entry.revenue += row.item_price ?? 0
        entry.count += 1
        todayMap.set(row.embedded_id, entry)
      }

      // Aggregate yesterday's sales per device
      const yesterdayMap = new Map<string, number>()
      const yesterdayRows = (yesterdaySalesRes.data ?? []) as { embedded_id: string; item_price: number }[]
      for (const row of yesterdayRows) {
        yesterdayMap.set(row.embedded_id, (yesterdayMap.get(row.embedded_id) ?? 0) + (row.item_price ?? 0))
      }

      // Dedupe paxcounter to latest per device
      const paxMap = new Map<string, number>()
      const paxRows = (paxRes.data ?? []) as { embedded_id: string; count: number }[]
      for (const row of paxRows) {
        if (!paxMap.has(row.embedded_id)) {
          paxMap.set(row.embedded_id, row.count)
        }
      }

      // Apply last sale results
      const machinesWithEmbedded = machines.value.filter(m => m.embeddeds?.id)
      for (let i = 0; i < machinesWithEmbedded.length; i++) {
        const machine = machinesWithEmbedded[i]!
        const saleData = lastSaleResults[i]?.data as { created_at: string; item_price: number; item_number: number } | null
        machine.last_sale_at = saleData?.created_at ?? null
        machine.last_sale_amount = saleData?.item_price ?? null
        machine.last_sale_item_number = saleData?.item_number ?? null
      }

      // Apply aggregated data to machines
      for (const machine of machines.value) {
        const eid = machine.embeddeds?.id
        if (!eid) continue
        const todayStats = todayMap.get(eid)
        machine.today_revenue = todayStats?.revenue ?? 0
        machine.today_sales_count = todayStats?.count ?? 0
        machine.yesterday_revenue = yesterdayMap.get(eid) ?? 0
        machine.paxcounter_count = paxMap.get(eid) ?? null
      }
    } finally {
      loading.value = false
    }
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
          const machine = machines.value.find(m => m.embeddeds?.id === sale.embedded_id)
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

  return { machines, loading, fetchMachines, subscribeToStatusUpdates }
}
