<template>
  <div class="p-6 space-y-6">

    <!-- HEADER -->
    <div>
      <h1 class="text-2xl font-bold text-gray-800">Home</h1>
      <p class="text-gray-500">Overview of your vending machines</p>
    </div>

    <!-- KPI CARDS -->
    <div class="grid grid-cols-1 md:grid-cols-4 gap-4">

      <div class="bg-white shadow rounded-xl p-4">
        <p class="text-sm text-gray-500">Sales Today</p>
        <p class="text-2xl font-semibold text-green-600">
          {{ formatCurrency(salesToday) }}
        </p>
      </div>

      <div class="bg-white shadow rounded-xl p-4">
        <p class="text-sm text-gray-500">Sales This Month</p>
        <p class="text-2xl font-semibold">
          {{ formatCurrency(salesMonth) }}
        </p>
      </div>

      <div class="bg-white shadow rounded-xl p-4">
        <p class="text-sm text-gray-500">Total Sales</p>
        <p class="text-2xl font-semibold">
          {{ totalSales }}
        </p>
      </div>

      <div class="bg-white shadow rounded-xl p-4">
        <p class="text-sm text-gray-500">Machines Active</p>
        <p class="text-2xl font-semibold text-blue-600">
          {{ machinesActive }}
        </p>
      </div>

    </div>

    <!-- SALES TABLE -->
    <div class="bg-white rounded-xl shadow overflow-hidden">

      <div class="p-4 border-b">
        <h2 class="font-semibold text-gray-700">Latest Sales</h2>
      </div>

      <table class="w-full text-sm">

        <thead class="bg-gray-50">
          <tr class="text-left text-gray-600">
            <th class="p-4">Time</th>
            <th class="p-4">Machine</th>
            <th class="p-4">Product</th>
            <th class="p-4">Channel</th>
            <th class="p-4">Price</th>
          </tr>
        </thead>

        <tbody>

          <tr
            v-for="sale in sales"
            :key="sale.id"
            class="border-t hover:bg-gray-50"
          >

            <td class="p-4 text-gray-500">
              {{ formatDate(sale.created_at) }}
            </td>

            <td class="p-4 font-medium text-gray-700">
              {{ sale.machines?.name || '-' }}
            </td>

            <td class="p-4 text-gray-500">
              {{ sale.product_id || '-' }}
            </td>

            <td class="p-4 text-gray-500">
              {{ sale.channel }}
            </td>

            <td class="p-4 font-semibold text-green-600">
              {{ formatCurrency(sale.item_price) }}
            </td>

          </tr>

        </tbody>

      </table>

      <div v-if="loading" class="p-6 text-center text-gray-500">
        Loading sales...
      </div>

      <div v-if="error" class="p-6 text-red-500">
        {{ error }}
      </div>

    </div>

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'

export default {

  name: "DashboardHome",

  data() {
    return {
      sales: [],
      loading: false,
      error: null,

      salesToday: 0,
      salesMonth: 0,
      totalSales: 0,
      machinesActive: 0
    }
  },

  async mounted() {
    await Promise.all([
      this.loadSales(),
      this.loadMetrics()
    ])
  },

  methods: {

    async loadSales() {

      this.loading = true

      const { data, error } = await supabase
        .from("sales")
        .select(`
          id,
          created_at,
          item_price,
          product_id,
          channel,
          machines (
            name
          )
        `)
        .order("created_at", { ascending: false })
        .limit(20)

      if (error) {
        this.error = error.message
      } else {
        this.sales = data
      }

      this.loading = false
    },

    async loadMetrics() {

      const today = new Date()
      today.setHours(0,0,0,0)

      const month = new Date()
      month.setDate(1)
      month.setHours(0,0,0,0)

      // SALES TODAY
      const { data: todayData } = await supabase
        .from("sales")
        .select("item_price")
        .gte("created_at", today.toISOString())

      // SALES MONTH
      const { data: monthData } = await supabase
        .from("sales")
        .select("item_price")
        .gte("created_at", month.toISOString())

      // TOTAL SALES
      const { count } = await supabase
        .from("sales")
        .select("*", { count: "exact", head: true })

      // MACHINES ACTIVE
      const { count: machines } = await supabase
        .from("machines")
        .select("*", { count: "exact", head: true })

      this.salesToday = todayData?.reduce((a,b)=>a + Number(b.item_price),0) || 0
      this.salesMonth = monthData?.reduce((a,b)=>a + Number(b.item_price),0) || 0
      this.totalSales = count || 0
      this.machinesActive = machines || 0
    },

    formatCurrency(value) {
      return new Intl.NumberFormat("pt-BR", {
        style: "currency",
        currency: "BRL"
      }).format(value)
    },

    formatDate(date) {
      return new Date(date).toLocaleString()
    }

  }

}
</script>