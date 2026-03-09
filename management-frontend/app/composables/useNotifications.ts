import { useSupabaseClient } from '#imports'

// ── Notification types ──────────────────────────────────────────────────────
export interface NotificationType {
  key: string
  label: string
  description: string
}

export const notificationTypes: NotificationType[] = [
  {
    key: 'sale',
    label: 'Sale notifications',
    description: 'Get notified for every vending machine sale.',
  },
  {
    key: 'low_stock',
    label: 'Low stock alerts',
    description: 'Get notified when a product drops below the refill threshold.',
  },
]

// ── Composable ──────────────────────────────────────────────────────────────
interface NotificationPreference {
  notification_type: string
  enabled: boolean
}

export interface PushDevice {
  id: string
  created_at: string
  endpoint: string
  user_agent: string | null
}

export function useNotifications() {
  const supabase = useSupabaseClient()
  const config = useRuntimeConfig()

  // Reactive state
  const permission = ref<NotificationPermission>('default')
  const isSubscribed = ref(false)
  const preferences = ref<NotificationPreference[]>([])
  const devices = ref<PushDevice[]>([])
  const loading = ref(false)
  const error = ref('')

  // iOS standalone detection
  const isIOSStandalone = ref(false)
  const isIOS = ref(false)

  // Update permission state
  function refreshPermission() {
    if (import.meta.server) return
    if ('Notification' in window) {
      permission.value = Notification.permission
    }
    // iOS detection
    const ua = navigator.userAgent
    isIOS.value = /iPad|iPhone|iPod/.test(ua) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1)
    isIOSStandalone.value = isIOS.value && (window.matchMedia('(display-mode: standalone)').matches || (navigator as any).standalone === true)
  }

  // Whether push notifications are supported
  const isSupported = computed(() => {
    if (import.meta.server) return false
    return 'serviceWorker' in navigator && 'PushManager' in window && 'Notification' in window
  })

  // Whether we need to show iOS homescreen guidance
  const needsHomescreen = computed(() => {
    return isIOS.value && !isIOSStandalone.value
  })

  // VAPID public key from runtime config
  const vapidPublicKey = computed(() => config.public.vapidPublicKey as string)

  // ── Subscribe to push notifications ─────────────────────────────────────
  async function subscribe() {
    error.value = ''
    if (!isSupported.value) {
      error.value = 'Push notifications are not supported in this browser.'
      console.warn('[Push] Not supported:', { sw: 'serviceWorker' in navigator, pm: 'PushManager' in window, notif: 'Notification' in window })
      return false
    }
    if (!vapidPublicKey.value) {
      error.value = 'Push notifications are not configured. VAPID_PUBLIC_KEY is missing.'
      console.warn('[Push] VAPID_PUBLIC_KEY missing')
      return false
    }

    loading.value = true
    try {
      // Check if permission was previously denied (iOS caches this even if
      // Notification.permission still reads 'default')
      const currentPermission = Notification.permission
      console.info('[Push] Current permission:', currentPermission)

      if (currentPermission === 'denied') {
        permission.value = 'denied'
        error.value = isIOS.value
          ? 'Notifications are blocked. Open Settings → VMflow → Notifications and enable them.'
          : 'Notifications are blocked. Allow them in your browser settings, then try again.'
        return false
      }

      // Request notification permission with a timeout for iOS edge cases
      // where the promise may hang if the user previously dismissed the prompt
      const result = await Promise.race([
        Notification.requestPermission(),
        new Promise<NotificationPermission>((_, reject) =>
          setTimeout(() => reject(new Error('timeout')), 10_000),
        ),
      ]).catch((err) => {
        console.warn('[Push] requestPermission failed/timed out:', err)
        return 'default' as NotificationPermission
      })

      console.info('[Push] Permission result:', result)
      permission.value = result

      if (result !== 'granted') {
        error.value = isIOS.value
          ? 'Permission not granted. If no prompt appeared, open Settings → VMflow → Notifications and enable them, then try again.'
          : 'Notification permission was denied.'
        return false
      }

      // Get service worker registration (with timeout — .ready never rejects,
      // it just waits forever if no SW is active)
      console.info('[Push] Waiting for service worker…')
      const registration = await Promise.race([
        navigator.serviceWorker.ready,
        new Promise<never>((_, reject) =>
          setTimeout(() => reject(new Error('Service worker not available. Try closing and reopening the app.')), 10_000),
        ),
      ])
      console.info('[Push] Service worker ready')

      // Subscribe to push manager
      console.info('[Push] Subscribing to push manager…')
      const subscription = await registration.pushManager.subscribe({
        userVisibleOnly: true,
        applicationServerKey: urlBase64ToUint8Array(vapidPublicKey.value),
      })
      console.info('[Push] Push subscription created')

      // Send subscription to backend
      console.info('[Push] Registering with backend…')
      const subJson = subscription.toJSON()
      const { error: fnError } = await supabase.functions.invoke('register-push', {
        method: 'POST',
        body: {
          endpoint: subJson.endpoint,
          keys: {
            p256dh: subJson.keys?.p256dh,
            auth: subJson.keys?.auth,
          },
        },
      })

      if (fnError) throw fnError

      console.info('[Push] Subscription registered with backend')
      isSubscribed.value = true
      await fetchDevices()
      return true
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : 'Failed to subscribe'
      error.value = msg
      console.warn('[Push] Subscribe failed:', err)
      return false
    } finally {
      loading.value = false
    }
  }

  // ── Unsubscribe from push notifications ─────────────────────────────────
  async function unsubscribe() {
    error.value = ''
    loading.value = true
    try {
      const registration = await navigator.serviceWorker.ready
      const subscription = await registration.pushManager.getSubscription()

      if (subscription) {
        // Remove from backend
        await supabase.functions.invoke('register-push', {
          method: 'DELETE',
          body: { endpoint: subscription.endpoint },
        })

        // Unsubscribe locally
        await subscription.unsubscribe()
      }

      isSubscribed.value = false
    } catch (err: unknown) {
      error.value = err instanceof Error ? err.message : 'Failed to unsubscribe'
    } finally {
      loading.value = false
    }
  }

  // ── Check current subscription state ────────────────────────────────────
  async function checkSubscription() {
    if (!isSupported.value) return
    try {
      // Use timeout — .ready hangs forever if no SW is active
      const registration = await Promise.race([
        navigator.serviceWorker.ready,
        new Promise<never>((_, reject) =>
          setTimeout(() => reject(new Error('SW not ready')), 5_000),
        ),
      ])
      const subscription = await registration.pushManager.getSubscription()
      isSubscribed.value = !!subscription
    } catch {
      isSubscribed.value = false
    }
  }

  // ── Fetch notification preferences ──────────────────────────────────────
  async function fetchPreferences() {
    try {
      const { data, error: fetchError } = await supabase
        .from('notification_preferences')
        .select('notification_type, enabled')

      if (fetchError) throw fetchError

      preferences.value = (data ?? []) as NotificationPreference[]
    } catch (err: unknown) {
      console.error('Failed to fetch notification preferences:', err)
    }
  }

  // ── Check if a notification type is enabled ─────────────────────────────
  // Default is enabled (absence of a row = enabled)
  function isTypeEnabled(type: string): boolean {
    const pref = preferences.value.find((p) => p.notification_type === type)
    return pref ? pref.enabled : true
  }

  // ── Toggle a notification type preference ───────────────────────────────
  async function togglePreference(type: string, enabled: boolean) {
    error.value = ''
    try {
      const user = useSupabaseUser()
      if (!user.value) return

      const userId = user.value.id ?? (user.value as any).sub

      const { error: upsertError } = await supabase
        .from('notification_preferences')
        .upsert(
          {
            user_id: userId,
            notification_type: type,
            enabled,
          },
          { onConflict: 'user_id,notification_type' },
        )

      if (upsertError) throw upsertError

      // Update local state
      const idx = preferences.value.findIndex((p) => p.notification_type === type)
      if (idx >= 0) {
        preferences.value[idx].enabled = enabled
      } else {
        preferences.value.push({ notification_type: type, enabled })
      }
    } catch (err: unknown) {
      error.value = err instanceof Error ? err.message : 'Failed to update preference'
    }
  }

  // ── Fetch registered push devices ─────────────────────────────────────
  async function fetchDevices() {
    try {
      const { data, error: fetchError } = await supabase
        .from('push_subscriptions')
        .select('id, created_at, endpoint, user_agent')
        .order('created_at', { ascending: false })

      if (fetchError) throw fetchError
      devices.value = (data ?? []) as PushDevice[]
    } catch (err: unknown) {
      console.error('Failed to fetch push devices:', err)
    }
  }

  // ── Remove a specific push subscription ──────────────────────────────
  async function removeDevice(id: string) {
    try {
      const { error: deleteError } = await supabase
        .from('push_subscriptions')
        .delete()
        .eq('id', id)

      if (deleteError) throw deleteError
      devices.value = devices.value.filter((d) => d.id !== id)

      // If we deleted our own current subscription, update state
      if (devices.value.length === 0) {
        isSubscribed.value = false
      }
    } catch (err: unknown) {
      error.value = err instanceof Error ? err.message : 'Failed to remove device'
    }
  }

  // ── Initialize (call on client mount) ───────────────────────────────────
  async function init() {
    if (import.meta.server) return
    refreshPermission()
    await Promise.all([checkSubscription(), fetchPreferences(), fetchDevices()])
  }

  return {
    // State
    permission,
    isSubscribed,
    preferences,
    devices,
    loading,
    error,
    isSupported,
    isIOS,
    isIOSStandalone,
    needsHomescreen,

    // Actions
    subscribe,
    unsubscribe,
    checkSubscription,
    fetchPreferences,
    fetchDevices,
    removeDevice,
    isTypeEnabled,
    togglePreference,
    init,
  }
}

// ── Helpers ─────────────────────────────────────────────────────────────────

function urlBase64ToUint8Array(base64String: string): Uint8Array {
  const padding = '='.repeat((4 - (base64String.length % 4)) % 4)
  const base64 = (base64String + padding).replace(/-/g, '+').replace(/_/g, '/')
  const rawData = atob(base64)
  const outputArray = new Uint8Array(rawData.length)
  for (let i = 0; i < rawData.length; ++i) {
    outputArray[i] = rawData.charCodeAt(i)
  }
  return outputArray
}
