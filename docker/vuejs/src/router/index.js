import { createRouter, createWebHistory } from 'vue-router'
import { supabase, isRecoveryRedirect } from '@/lib/supabase'

import Home from '@/views/Home.vue'
import Login from '@/views/Login.vue'
import Register from '@/views/Register.vue'
import ForgotPassword from '@/views/ForgotPassword.vue'
import ResetPassword from '@/views/ResetPassword.vue'
import Payment from '@/views/Payment.vue'
import PaymentML from '@/views/PaymentML.vue'

import Dashboard from '@/views/Dashboard.vue'
import DashboardHome from '@/views/Dashboard_Home.vue'
import DashboardMachines from '@/views/Dashboard_Machines.vue'
import DashboardMachineModels from '@/views/Dashboard_MachineModels.vue'
import DashboardProducts from '@/views/Dashboard_Products.vue'
import DashboardEmbedded from '@/views/Dashboard_Embedded.vue'
import DashboardSettings from '@/views/Dashboard_Settings.vue'
import DashboardMetrics from '@/views/Dashboard_Metrics.vue'
import DashboardInventory from '@/views/Dashboard_Inventory.vue'
import DashboardMap from '@/views/Dashboard_Map.vue'
import DashboardArcade from '@/views/Dashboard_Arcade.vue'

const routes = [
  { path: '/', redirect: '/home' },
  { path: '/home', component: Home },
  { path: '/pay/stripe/:machineId', component: Payment,   name: 'PaymentStripe' },
  { path: '/pay/ml/:machineId',     component: PaymentML, name: 'PaymentML' },
  { path: '/pay/:machineId', redirect: to => `/pay/stripe/${to.params.machineId}` },
  {
    path: '/dashboard',
    component: Dashboard,
    meta: { requiresAuth: true },
    children: [
      { path: '',               component: DashboardHome,         name: 'Home' },
      { path: 'machines',       component: DashboardMachines,     name: 'Machines' },
      { path: 'machine-models', component: DashboardMachineModels,name: 'Machine Models' },
      { path: 'products',       component: DashboardProducts,     name: 'Products' },
      { path: 'settings',       component: DashboardSettings,     name: 'Settings' },
      { path: 'metrics',        component: DashboardMetrics,      name: 'Metrics' },
      { path: 'devices',        component: DashboardEmbedded,     name: 'Devices' },
      { path: 'inventory',      component: DashboardInventory,    name: 'Inventory' },
      { path: 'map',            component: DashboardMap,          name: 'Map' },
      { path: 'arcade',         component: DashboardArcade,       name: 'Arcade' }
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

let pendingRecovery = isRecoveryRedirect

router.beforeEach(async (to, from, next) => {
  // getSession waits for the client to exchange the recovery tokens in the URL hash
  const { data: { session } } = await supabase.auth.getSession()

  // Recovery links land on SITE_URL (e.g. when sent from Studio), so send them to the reset page once
  if (pendingRecovery) {
    pendingRecovery = false
    if (to.path !== '/reset-password') {
      next('/reset-password')
      return
    }
  }

  if (to.meta.requiresAuth && !session) {
    next('/login')
  } else if ((to.path === '/login' || to.path === '/register' || to.path === '/forgot-password') && session) {
    next('/dashboard')
  } else {
    next()
  }
})

export default router