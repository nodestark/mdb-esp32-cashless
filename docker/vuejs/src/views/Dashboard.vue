<template>
  <div class="flex h-screen bg-gray-100">

    <!-- SIDEBAR -->
    <aside class="w-64 bg-slate-900 text-slate-200 flex flex-col">

      <!-- LOGO -->
      <div class="px-6 py-5 border-b border-slate-800">
        <h1 class="text-xl font-bold tracking-wide">
          VMflow
        </h1>
        <p class="text-xs text-slate-400">
          Vending Management
        </p>
      </div>

      <!-- MENU -->
      <nav class="flex-1 px-4 py-6 space-y-2">

        <RouterLink
          to="/dashboard"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>🏠</span>
          Home
        </RouterLink>

        <RouterLink
          to="/dashboard/machines"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>🤖</span>
          Machines
        </RouterLink>

        <RouterLink
          to="/dashboard/machine-models"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>🧩</span>
          Machine Models
        </RouterLink>

        <RouterLink
          to="/dashboard/devices"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>📡</span>
          Devices
        </RouterLink>

        <RouterLink
          to="/dashboard/inventory"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>📦</span>
          Inventory
        </RouterLink>

        <RouterLink
          to="/dashboard/products"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>🏷️</span>
          Products
        </RouterLink>

        <RouterLink
          to="/dashboard/map"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>📍</span>
          Map
        </RouterLink>

        <RouterLink
          to="/dashboard/metrics"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>📊</span>
          Metrics
        </RouterLink>

        <RouterLink
          to="/dashboard/settings"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>⚙️</span> Settings
        </RouterLink>

      </nav>

      <!-- USER AREA -->
      <div class="border-t border-slate-800 p-4">

        <button
          @click="logout"
          class="w-full flex items-center justify-center gap-2 py-2 text-sm rounded-lg bg-slate-800 hover:bg-red-600 transition"
        >
          ⎋ Logout
        </button>

      </div>

    </aside>


    <!-- MAIN AREA -->
    <div class="flex flex-col flex-1">

      <!-- TOPBAR -->
      <header class="h-16 bg-white border-b flex items-center justify-between px-8">

        <div class="text-sm font-medium text-gray-700">
          {{ $route.name ?? 'Dashboard' }}
        </div>

        <div class="flex items-center gap-4">

          <div class="text-sm text-gray-600">
            {{ userEmail }}
          </div>

          <div class="w-8 h-8 rounded-full bg-slate-300 flex items-center justify-center text-sm">
            {{ userInitial }}
          </div>

        </div>

      </header>

      <!-- CONTENT -->
      <main class="flex-1 p-8 overflow-y-auto">
        <RouterView />
      </main>

    </div>

  <!-- EASTER EGG ARCADE MODAL -->
  <div
    v-if="showArcade"
    class="fixed inset-0 bg-black/80 flex items-center justify-center z-50"
    @click.self="showArcade = false"
  >
    <div class="flex flex-col items-center gap-4">
      <div class="flex items-center justify-between w-full px-1">
        <span class="text-green-400 font-mono text-sm tracking-widest">⚡ VMflow Runner</span>
        <button @click="showArcade = false" class="text-slate-400 hover:text-white text-xl leading-none">✕</button>
      </div>
      <iframe
        ref="arcadeFrame"
        src="/arcade/index.html"
        class="rounded-xl border-2 border-slate-700 shadow-2xl"
        style="width: 860px; max-width: 95vw; height: 380px; background: #0f172a;"
        frameborder="0"
      />
      <p class="text-slate-600 text-xs font-mono">ESC or click outside to close</p>
    </div>
  </div>

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'
import { useRouter } from 'vue-router'
import { ref, onMounted, onBeforeUnmount, nextTick } from 'vue'
import { startAlerts, stopAlerts } from '@/lib/useAlerts'

export default {
  name: 'Dashboard',

  setup() {

    const router = useRouter()

    const userEmail   = ref("")
    const userInitial = ref("?")
    const showArcade  = ref(false)
    const arcadeFrame = ref(null)

    let keyBuffer = ''
    const KONAMI  = 'vmflow'

    const onKey = (e) => {
      if (e.key === 'Escape') { showArcade.value = false; return }
      keyBuffer = (keyBuffer + e.key.toLowerCase()).slice(-KONAMI.length)
      if (keyBuffer === KONAMI) {
        showArcade.value = true
        keyBuffer = ''
        nextTick(() => arcadeFrame.value?.focus())
      }
    }

    const logout = async () => {
      await supabase.auth.signOut()
      router.push('/login')
    }

    const loadUser = async () => {
      const { data } = await supabase.auth.getUser()

      if (data?.user?.email) {
        userEmail.value = data.user.email
        userInitial.value = data.user.email.charAt(0).toUpperCase()
      }
    }

    const initNotifications = async () => {
      if (!('Notification' in window)) return
      if (Notification.permission === 'default') {
        await Notification.requestPermission()
      }
      startAlerts()
    }

    onMounted(() => {
      loadUser()
      initNotifications()
      window.addEventListener('keydown', onKey)
    })

    onBeforeUnmount(() => {
      stopAlerts()
      window.removeEventListener('keydown', onKey)
    })

    return {
      logout,
      userEmail,
      userInitial,
      showArcade,
      arcadeFrame
    }
  }
}
</script>