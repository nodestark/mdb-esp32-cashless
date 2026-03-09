import { createRouter, createWebHistory } from 'vue-router'
import { supabase } from '@/lib/supabase'

import Home from '@/views/Home.vue'
import Login from '@/views/Login.vue'
import Register from '@/views/Register.vue'

import Dashboard from '@/views/Dashboard.vue'
import DashboardHome from '@/views/Dashboard_Home.vue'
import DashboardMachines from '@/views/Dashboard_Machines.vue'
import DashboardEmbedded from '@/views/Dashboard_Embedded.vue'
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
      { path: 'metrics', component: DashboardMetrics },
      { path: 'devices', component: DashboardEmbedded }
    ]
  },
  { path: '/login', component: Login },
  { path: '/register', component: Register }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach(async (to, from, next) => {
  const { data: { session } } = await supabase.auth.getSession()

  if (to.meta.requiresAuth && !session) {
    next('/login')
  } else if ((to.path === '/login' || to.path === '/register') && session) {
    next('/dashboard')
  } else {
    next()
  }
})

export default router