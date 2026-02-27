<template>
  <div class="flex h-screen bg-gray-100">

    <!-- Sidebar -->
    <aside class="w-64 bg-slate-800 text-white flex flex-col justify-between p-6">

      <div>
        <h3 class="text-lg font-semibold mb-6">
          Menu
        </h3>

        <nav class="space-y-2">

          <RouterLink
            to="/dashboard"
            class="block px-4 py-2 rounded-lg hover:bg-slate-700 transition"
            active-class="bg-slate-700"
          >
            Home
          </RouterLink>

          <RouterLink
            to="/dashboard/reports"
            class="block px-4 py-2 rounded-lg hover:bg-slate-700 transition"
            active-class="bg-slate-700"
          >
            Relatórios
          </RouterLink>

          <RouterLink
            to="/dashboard/settings"
            class="block px-4 py-2 rounded-lg hover:bg-slate-700 transition"
            active-class="bg-slate-700"
          >
            Configurações
          </RouterLink>

        </nav>
      </div>

      <!-- Logout -->
      <button
        @click="logout"
        class="mt-6 w-full py-3 bg-red-600 rounded-lg hover:bg-red-700 transition"
      >
        Sair
      </button>

    </aside>

    <!-- Main Content -->
    <main class="flex-1 p-10 overflow-y-auto">
      <RouterView />
    </main>

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'
import { useRouter } from 'vue-router'

export default {
  name: 'Dashboard',
  setup() {
    const router = useRouter()

    const logout = async () => {
      await supabase.auth.signOut()
      router.push('/login')
    }

    return { logout }
  }
}
</script>