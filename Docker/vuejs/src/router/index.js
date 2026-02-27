import { createRouter, createWebHistory } from 'vue-router'
import { supabase } from '@/lib/supabase'

import Home from '@/views/Home.vue'
import Login from '@/views/Login.vue'
import Register from '@/views/Register.vue'

import Dashboard from '@/views/Dashboard.vue'
import DashboardHome from '@/views/Dashboard_Home.vue'
import DashboardReports from '@/views/Dashboard_Reports.vue'
import DashboardSettings from '@/views/Dashboard_Settings.vue'

const routes = [
  { path: '/', redirect: '/home' },
  { path: '/home', component: Home },
  { path: '/dashboard', component: Dashboard,
    meta: { requiresAuth: true },
    children: [
      { path: '', component: DashboardHome },
      { path: 'reports', component: DashboardReports },
      { path: 'settings', component: DashboardSettings }
    ] },
  { path: '/dashboard/home', component: DashboardHome },
  { path: '/login', component: Login },
  { path: '/register', component: Register }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

// Navigation Guard
router.beforeEach(async (to, from, next) => {
  const { data: { session } } = await supabase.auth.getSession()

  if (to.meta.requiresAuth && !session) {
    next('/login')
  } else {
    next()
  }
})

export default router