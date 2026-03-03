/**
 * Handles iOS PWA (and general browser) resume from background.
 *
 * When the app becomes visible again after being backgrounded:
 * 1. Refreshes the Supabase session (prevents "session expired" re-login)
 * 2. Calls any registered refresh callbacks so pages can re-fetch data
 *
 * Usage in pages:
 *   const { onResume } = useAppResume()
 *   onResume(() => fetchMachines())
 */

type ResumeCallback = () => void | Promise<void>

// Global state: session refresh runs once, shared across all component instances
const lastRefresh = ref(0)
const MIN_BACKGROUND_MS = 30_000 // only refresh if backgrounded > 30s

export function useAppResume() {
  const callbacks = new Set<ResumeCallback>()

  function onResume(cb: ResumeCallback) {
    callbacks.add(cb)
    // Auto-cleanup when the component unmounts
    if (getCurrentInstance()) {
      onUnmounted(() => callbacks.delete(cb))
    }
  }

  if (import.meta.client) {
    let hiddenAt = 0

    const handleVisibilityChange = async () => {
      if (document.visibilityState === 'hidden') {
        hiddenAt = Date.now()
        return
      }

      // Visible again — check if we were gone long enough
      const elapsed = hiddenAt ? Date.now() - hiddenAt : 0
      if (elapsed < MIN_BACKGROUND_MS) return

      // Debounce: don't refresh more than once per 30s
      if (Date.now() - lastRefresh.value < MIN_BACKGROUND_MS) return
      lastRefresh.value = Date.now()

      // 1. Refresh the Supabase session token
      try {
        const supabase = useSupabaseClient()
        const { error } = await supabase.auth.refreshSession()
        if (error) {
          console.warn('[app-resume] Session refresh failed:', error.message)
          // If refresh fails, redirect to login
          await navigateTo('/auth/login')
          return
        }
      } catch (e) {
        console.warn('[app-resume] Session refresh error:', e)
        await navigateTo('/auth/login')
        return
      }

      // 2. Run all registered data-refresh callbacks
      for (const cb of callbacks) {
        try {
          await cb()
        } catch (e) {
          console.warn('[app-resume] Refresh callback error:', e)
        }
      }
    }

    onMounted(() => {
      document.addEventListener('visibilitychange', handleVisibilityChange)
    })

    onUnmounted(() => {
      document.removeEventListener('visibilitychange', handleVisibilityChange)
    })
  }

  return { onResume }
}
