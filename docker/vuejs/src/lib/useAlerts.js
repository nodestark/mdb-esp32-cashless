import { ref } from 'vue'
import { supabase } from '@/lib/supabase'
import { lowStockThreshold } from '@/lib/settings'

let channel = null

// 'granted' | 'denied' | 'default' (not asked yet) | 'unsupported'
export const notificationPermission = ref(
  'Notification' in window ? Notification.permission : 'unsupported'
)

function notify(title, body) {
  if (Notification.permission !== 'granted') return
  new Notification(title, { body, icon: '/favicon.svg' })
}

/**
 * Asks for permission from a user click (Settings). Browsers ignore or
 * penalise prompts that appear on page load, and a "deny" is final.
 */
export async function enableNotifications() {
  if (notificationPermission.value === 'unsupported') return
  notificationPermission.value = await Notification.requestPermission()
  if (notificationPermission.value === 'granted') await startAlerts()
}

/** Listens for low stock and offline devices. Does nothing without permission. */
export async function startAlerts() {
  if (channel || notificationPermission.value !== 'granted') return
  const { data: { user } } = await supabase.auth.getUser()
  if (!user) return

  channel = supabase
    .channel(`vmflow-alerts-${user.id}`)

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
