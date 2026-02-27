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

      // Enrich with last sale per machine
      for (const machine of machines.value) {
        if (machine.embeddeds?.id) {
          const { data: saleData } = await supabase
            .from('sales')
            .select('created_at, item_price')
            .eq('embedded_id', machine.embeddeds.id)
            .order('created_at', { ascending: false })
            .limit(1)
            .maybeSingle()

          machine.last_sale_at = saleData?.created_at ?? null
          machine.last_sale_amount = saleData?.item_price ?? null
        }
      }
    } finally {
      loading.value = false
    }
  }

  function subscribeToStatusUpdates() {
    const supabase = useSupabaseClient()
    const channel = supabase
      .channel('embeddeds-status')
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
      .subscribe()

    return () => supabase.removeChannel(channel)
  }

  return { machines, loading, fetchMachines, subscribeToStatusUpdates }
}
