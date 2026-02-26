import { createRouter, createWebHistory } from 'vue-router'

import Dashboard from '../views/Dashboard.vue'
import Embedded from '../views/Embedded.vue'

const routes = [
  { path: '/', component: Dashboard },
  { path: '/embedded', component: Embedded }
]

export default createRouter({
  history: createWebHistory(),
  routes
})