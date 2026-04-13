import { createRouter, createWebHistory } from 'vue-router'
import { supabase } from '@/lib/supabase'

import Home from '@/views/Home.vue'
import Login from '@/views/Login.vue'
import Register from '@/views/Register.vue'
import ForgotPassword from '@/views/ForgotPassword.vue'
import ResetPassword from '@/views/ResetPassword.vue'

import Dashboard from '@/views/Dashboard.vue'
import DashboardHome from '@/views/Dashboard_Home.vue'
import DashboardMachines from '@/views/Dashboard_Machines.vue'
import DashboardMachineModels from '@/views/Dashboard_MachineModels.vue'
import DashboardProducts from '@/views/Dashboard_Products.vue'
import DashboardEmbedded from '@/views/Dashboard_Embedded.vue'
import DashboardSettings from '@/views/Dashboard_Settings.vue'
import DashboardMetrics from '@/views/Dashboard_Metrics.vue' // ✅ typo corrigido

const routes = [
  { path: '/', redirect: '/home' },
  { path: '/home', component: Home },
  {
    path: '/dashboard',
    component: Dashboard,
    meta: { requiresAuth: true },
    children: [
      { path: '', component: DashboardHome },
      { path: 'machines', component: DashboardMachines },
      { path: 'machine-models', component: DashboardMachineModels },
      { path: 'products', component: DashboardProducts },
      { path: 'settings', component: DashboardSettings },
      { path: 'metrics', component: DashboardMetrics },
      { path: 'devices', component: DashboardEmbedded }
    ]
  },
  { path: '/login', component: Login },
  { path: '/register', component: Register },
  { path: '/forgot-password', component: ForgotPassword },
  { path: '/reset-password', component: ResetPassword }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach(async (to, from, next) => {
  // Check if this is a recovery redirect from Supabase (hash fragment contains type=recovery)
  const hash = window.location.hash
  if (hash && hash.includes('type=recovery')) {
    next('/reset-password')
    return
  }

  const { data: { session } } = await supabase.auth.getSession()

  if (to.meta.requiresAuth && !session) {
    next('/login')
  } else if ((to.path === '/login' || to.path === '/register' || to.path === '/forgot-password') && session) {
    next('/dashboard')
  } else {
    next()
  }
})

export default router