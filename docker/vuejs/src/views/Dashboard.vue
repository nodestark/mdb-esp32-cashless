<template>
  <div class="flex h-screen bg-gray-100">

    <!-- SIDEBAR -->
    <aside class="w-64 bg-slate-900 text-slate-200 flex flex-col">

      <!-- LOGO -->
      <div class="px-6 py-5 border-b border-slate-800">
        <h1 class="text-xl font-bold tracking-wide">
          VMFlow
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
          <span>🥤</span>
          Machines
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
          to="/dashboard/metrics"
          class="flex items-center gap-3 px-4 py-2 rounded-lg hover:bg-slate-800 transition"
          active-class="bg-slate-800 text-white"
        >
          <span>📊</span>
          Metrics
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

        <div class="text-sm text-gray-500">
          Dashboard
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

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'
import { useRouter } from 'vue-router'
import { ref, onMounted } from 'vue'

export default {
  name: 'Dashboard',

  setup() {

    const router = useRouter()

    const userEmail = ref("")
    const userInitial = ref("?")

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

    onMounted(loadUser)

    return {
      logout,
      userEmail,
      userInitial
    }
  }
}
</script>