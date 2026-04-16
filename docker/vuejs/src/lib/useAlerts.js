import { supabase } from '@/lib/supabase'
import { lowStockThreshold } from '@/lib/settings'

let channel = null

function notify(title, body) {
  if (Notification.permission !== 'granted') return
  new Notification(title, { body, icon: '/favicon.ico' })
}

export function startAlerts() {
  channel = supabase
    .channel('vmflow-alerts')

    .on(
      'postgres_changes',
      { event: 'UPDATE', schema: 'public', table: 'machine_coils' },
      async ({ new: coil }) => {
        if (!coil.capacity || coil.current_stock == null) return
        const pct = Math.round((coil.current_stock / coil.capacity) * 100)
        if (pct > lowStockThreshold.value) return

        const { data: machine } = await supabase
          .from('machines')
          .select('name')
          .eq('id', coil.machine_id)
          .single()

        const { data: product } = coil.product_id
          ? await supabase.from('products').select('name').eq('id', coil.product_id).single()
          : { data: null }

        const machineName = machine?.name ?? 'Unknown machine'
        const productName = product?.name ?? `Coil ${coil.alias ?? coil.item_number}`
        const label = pct === 0 ? 'Empty' : `Low (${pct}%)`

        notify(`⚠️ ${label} — ${machineName}`, productName)
      }
    )

    .on(
      'postgres_changes',
      { event: 'UPDATE', schema: 'public', table: 'embedded' },
      async ({ new: device, old: prev }) => {
        if (prev.status === device.status) return
        if (device.status !== 'offline') return

        const { data: machine } = await supabase
          .from('machines')
          .select('name')
          .eq('id', device.machine_id)
          .single()

        notify('🔴 Device offline', machine?.name ?? device.subdomain)
      }
    )

    .subscribe()
}

export function stopAlerts() {
  if (channel) {
    supabase.removeChannel(channel)
    channel = null
  }
}
