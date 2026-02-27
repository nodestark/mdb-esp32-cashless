<template>
  <div class="dashboard">

    <!-- Menu lateral ESQUERDO -->
    <aside class="sidebar">
      <div>
        <h3>Menu</h3>

        <RouterLink to="/dashboard" class="menu-item">
          Home
        </RouterLink>

        <RouterLink to="/dashboard/reports" class="menu-item">
          Relatórios
        </RouterLink>

        <RouterLink to="/dashboard/settings" class="menu-item">
          Configurações
        </RouterLink>
      </div>

      <!-- Botão Sair -->
      <button class="logout-btn" @click="logout">
        Sair
      </button>
    </aside>

    <!-- Área dinâmica DIREITA -->
    <main class="content">
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

<style scoped>
.dashboard {
  display: flex;
  height: 100vh;
}

/* Menu esquerdo */
.sidebar {
  width: 250px;
  background: #1e293b;
  color: white;
  display: flex;
  flex-direction: column;
  justify-content: space-between; /* mantém o botão embaixo */
  padding: 20px;
}

/* Área direita */
.content {
  flex: 1;
  padding: 30px;
  background: #f5f5f5;
  overflow-y: auto;
}

/* Links */
.menu-item {
  display: block;
  color: white;
  text-decoration: none;
  margin-bottom: 10px;
  padding: 8px;
  border-radius: 4px;
}

.menu-item.router-link-active {
  background: #334155;
}

/* Botão sair */
.logout-btn {
  background: #dc2626;
  border: none;
  padding: 10px;
  color: white;
  border-radius: 4px;
  cursor: pointer;
}

.logout-btn:hover {
  background: #b91c1c;
}
</style>