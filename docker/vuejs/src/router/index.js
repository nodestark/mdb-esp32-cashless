import { createRouter, createWebHistory } from 'vue-router'
import { supabase, isRecoveryRedirect } from '@/lib/supabase'

const routes = [
  { path: '/', redirect: '/home' },
  { path: '/home', component: () => import('@/views/Home.vue') },
  { path: '/pay/stripe/:machineId', component: () => import('@/views/Payment.vue'),   name: 'PaymentStripe' },
  { path: '/pay/ml/:machineId',     component: () => import('@/views/PaymentML.vue'), name: 'PaymentML' },
  { path: '/pay/:machineId', redirect: to => `/pay/stripe/${to.params.machineId}` },
  {
    path: '/dashboard',
    component: () => import('@/views/Dashboard.vue'),
    meta: { requiresAuth: true },
    children: [
      { path: '',               component: () => import('@/views/Dashboard_Home.vue'),         name: 'Home' },
      { path: 'machines',       component: () => import('@/views/Dashboard_Machines.vue'),     name: 'Machines' },
      { path: 'machine-models', component: () => import('@/views/Dashboard_MachineModels.vue'),name: 'Machine Models' },
      { path: 'products',       component: () => import('@/views/Dashboard_Products.vue'),     name: 'Products' },
      { path: 'settings',       component: () => import('@/views/Dashboard_Settings.vue'),     name: 'Settings' },
      { path: 'metrics',        component: () => import('@/views/Dashboard_Metrics.vue'),      name: 'Metrics' },
      { path: 'devices',        component: () => import('@/views/Dashboard_Embedded.vue'),     name: 'Devices' },
      { path: 'inventory',      component: () => import('@/views/Dashboard_Inventory.vue'),    name: 'Inventory' },
      { path: 'map',            component: () => import('@/views/Dashboard_Map.vue'),          name: 'Map' },
      { path: 'arcade',         component: () => import('@/views/Dashboard_Arcade.vue'),       name: 'Arcade' }
    ]
  },
  { path: '/login', component: () => import('@/views/Login.vue') },
  { path: '/register', component: () => import('@/views/Register.vue') },
  { path: '/forgot-password', component: () => import('@/views/ForgotPassword.vue') },
  { path: '/reset-password', component: () => import('@/views/ResetPassword.vue') }
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

// Views load on demand. After a new deploy, a tab opened before it asks for
// chunks that no longer exist: reload once into the new version.
router.onError((error, to) => {
  const stale = /Failed to fetch dynamically imported module|Importing a module script failed|error loading dynamically imported module/i
  if (stale.test(error?.message ?? '') && !sessionStorage.getItem('vmflow_chunk_reload')) {
    sessionStorage.setItem('vmflow_chunk_reload', '1')
    window.location.assign(to.fullPath)
  }
})

router.afterEach(() => sessionStorage.removeItem('vmflow_chunk_reload'))

export default router