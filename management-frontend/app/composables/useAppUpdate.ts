/**
 * Detects when a new service worker version is available
 * and provides a way to activate it (skip waiting + reload).
 */
export function useAppUpdate() {
  const updateAvailable = ref(false)
  const registration = ref<ServiceWorkerRegistration | null>(null)

  function applyUpdate() {
    const waiting = registration.value?.waiting
    if (waiting) {
      waiting.postMessage({ type: 'SKIP_WAITING' })
    }
    // Reload once the new SW takes control
    if (import.meta.client) {
      navigator.serviceWorker?.addEventListener('controllerchange', () => {
        window.location.reload()
      }, { once: true })
    }
  }

  if (import.meta.client && 'serviceWorker' in navigator) {
    navigator.serviceWorker.ready.then((reg) => {
      registration.value = reg

      // Check if there's already a waiting worker (e.g. from a previous visit)
      if (reg.waiting) {
        updateAvailable.value = true
      }

      // Listen for new service worker installations
      reg.addEventListener('updatefound', () => {
        const newWorker = reg.installing
        if (!newWorker) return

        newWorker.addEventListener('statechange', () => {
          // New SW installed and waiting to activate
          if (newWorker.state === 'installed' && navigator.serviceWorker.controller) {
            updateAvailable.value = true
          }
        })
      })
    })
  }

  return {
    updateAvailable,
    applyUpdate,
  }
}
