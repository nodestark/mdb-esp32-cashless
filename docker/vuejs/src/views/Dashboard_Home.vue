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
        <p class="text-2xl font-semibold text-green-600">{{ formatCurrency(salesToday) }}</p>
        <p v-if="todayVariation !== null" class="text-xs mt-1" :class="variationClass(todayVariation)">
          {{ variationLabel(todayVariation) }} vs yesterday
        </p>
        <p v-else class="text-xs mt-1 text-gray-400">no data yesterday</p>
      </div>

      <div class="bg-white shadow rounded-xl p-4">
        <p class="text-sm text-gray-500">Sales This Month</p>
        <p class="text-2xl font-semibold">{{ formatCurrency(salesMonth) }}</p>
        <p class="text-xs mt-1 text-gray-400">current month</p>
      </div>

      <div class="bg-white shadow rounded-xl p-4">
        <p class="text-sm text-gray-500">Total Sales</p>
        <p class="text-2xl font-semibold">{{ totalSales }}</p>
        <p class="text-xs mt-1 text-gray-400">all time</p>
      </div>

      <div class="bg-white shadow rounded-xl p-4">
        <p class="text-sm text-gray-500">Machines Online</p>
        <p class="text-2xl font-semibold text-blue-600">{{ machinesOnline }}<span class="text-base text-gray-400"> / {{ machinesActive }}</span></p>
        <p class="text-xs mt-1 text-gray-400">online / total</p>
      </div>

    </div>

    <!-- SALES TABLE -->
    <div class="bg-white rounded-xl shadow overflow-hidden">

      <!-- TABLE HEADER -->
      <div class="p-4 border-b flex flex-wrap items-center justify-between gap-3">
        <h2 class="font-semibold text-gray-700">Latest Sales</h2>

        <div class="flex items-center gap-2 text-sm">
          <input
            v-model="dateFrom"
            type="date"
            class="border rounded px-2 py-1 text-gray-600 focus:outline-none focus:ring-2 focus:ring-blue-300"
            @change="applyFilter"
          />
          <span class="text-gray-400">—</span>
          <input
            v-model="dateTo"
            type="date"
            class="border rounded px-2 py-1 text-gray-600 focus:outline-none focus:ring-2 focus:ring-blue-300"
            @change="applyFilter"
          />
          <button
            v-if="dateFrom || dateTo"
            @click="clearFilter"
            class="px-2 py-1 text-gray-400 hover:text-gray-600 transition"
            title="Clear filter"
          >
            ✕
          </button>
        </div>
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
            <td class="p-4 text-gray-500">{{ formatDate(sale.created_at) }}</td>

            <td class="p-4 font-medium text-gray-700">{{ sale.machines?.name || '-' }}</td>

            <td class="p-4 text-gray-500">
              {{ sale.products?.name || '-' }}
              <span v-if="sale.coil_alias" class="ml-1 px-1.5 py-0.5 rounded text-xs bg-gray-100 text-gray-500 font-mono">
                {{ sale.coil_alias }}
              </span>
            </td>

            <td class="p-4">
              <span :class="channelClass(sale.channel)">{{ sale.channel }}</span>
            </td>

            <td class="p-4 font-semibold text-green-600">{{ formatCurrency(sale.item_price) }}</td>
          </tr>

          <tr v-if="!loading && sales.length === 0">
            <td colspan="5" class="p-8 text-center text-gray-400">No sales recorded yet.</td>
          </tr>

        </tbody>

      </table>

      <div v-if="loading" class="p-6 text-center text-gray-500">Loading sales...</div>
      <div v-if="error" class="p-6 text-red-500">{{ error }}</div>

      <!-- PAGINATION -->
      <div v-if="totalCount > pageSize" class="p-4 border-t flex items-center justify-between text-sm text-gray-500">
        <span>{{ paginationLabel }}</span>
        <div class="flex gap-2">
          <button
            :disabled="page === 0"
            @click="goToPage(page - 1)"
            class="px-3 py-1 border rounded hover:bg-gray-50 disabled:opacity-40 transition"
          >
            ← Prev
          </button>
          <button
            :disabled="(page + 1) * pageSize >= totalCount"
            @click="goToPage(page + 1)"
            class="px-3 py-1 border rounded hover:bg-gray-50 disabled:opacity-40 transition"
          >
            Next →
          </button>
        </div>
      </div>

    </div>

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'

const PAGE_SIZE = 20

export default {

  name: "DashboardHome",

  data() {
    return {
      sales: [],
      loading: false,
      error: null,

      page: 0,
      pageSize: PAGE_SIZE,
      totalCount: 0,

      dateFrom: '',
      dateTo: '',

      salesToday: 0,
      salesMonth: 0,
      totalSales: 0,
      machinesActive: 0,
      machinesOnline: 0,
      todayVariation: null
    }
  },

  computed: {
    paginationLabel() {
      const from = this.page * this.pageSize + 1
      const to = Math.min((this.page + 1) * this.pageSize, this.totalCount)
      return `${from}–${to} of ${this.totalCount}`
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
      this.error = null

      const from = this.page * this.pageSize
      const to = from + this.pageSize - 1

      let query = supabase
        .from("sales")
        .select(`
          id,
          created_at,
          item_price,
          channel,
          coil_alias,
          machines!sale_machine_id_fkey (name),
          products!sale_product_id_fkey (name)
        `, { count: 'exact' })
        .order("created_at", { ascending: false })
        .range(from, to)

      if (this.dateFrom) {
        query = query.gte("created_at", new Date(this.dateFrom).toISOString())
      }
      if (this.dateTo) {
        const end = new Date(this.dateTo)
        end.setHours(23, 59, 59, 999)
        query = query.lte("created_at", end.toISOString())
      }

      const { data, error, count } = await query

      if (error) {
        this.error = error.message
      } else {
        this.sales = data
        this.totalCount = count ?? 0
      }

      this.loading = false
    },

    applyFilter() {
      this.page = 0
      this.loadSales()
    },

    clearFilter() {
      this.dateFrom = ''
      this.dateTo = ''
      this.page = 0
      this.loadSales()
    },

    goToPage(p) {
      this.page = p
      this.loadSales()
    },

    sumPrices(data) {
      return data?.reduce((a, b) => a + Number(b.item_price), 0) || 0
    },

    calcVariation(current, previous) {
      if (!previous) return null
      return Math.round(((current - previous) / previous) * 100)
    },

    variationLabel(pct) {
      if (pct === null) return '—'
      if (pct === 0) return '0%'
      return (pct > 0 ? '↑ +' : '↓ ') + pct + '%'
    },

    variationClass(pct) {
      if (pct === null || pct === 0) return 'text-gray-400'
      return pct > 0 ? 'text-green-500' : 'text-red-500'
    },

    async loadMetrics() {
      const now = new Date()
      const todayStart     = new Date(now); todayStart.setHours(0, 0, 0, 0)
      const yesterdayStart = new Date(todayStart); yesterdayStart.setDate(yesterdayStart.getDate() - 1)
      const monthStart     = new Date(now); monthStart.setDate(1); monthStart.setHours(0, 0, 0, 0)

      const [
        { data: todayData },
        { data: yesterdayData },
        { data: monthData },
        { count },
        { data: embeddeds }
      ] = await Promise.all([
        supabase.from("sales").select("item_price").gte("created_at", todayStart.toISOString()),
        supabase.from("sales").select("item_price").gte("created_at", yesterdayStart.toISOString()).lt("created_at", todayStart.toISOString()),
        supabase.from("sales").select("item_price").gte("created_at", monthStart.toISOString()),
        supabase.from("sales").select("*", { count: "exact", head: true }),
        supabase.from("embedded").select("machine_id, status")
      ])

      const linked = (embeddeds ?? []).filter(e => e.machine_id)
      this.salesToday     = this.sumPrices(todayData)
      this.salesMonth     = this.sumPrices(monthData)
      this.totalSales     = count || 0
      this.machinesActive = linked.length
      this.machinesOnline = linked.filter(e => e.status === 'online').length
      this.todayVariation = this.calcVariation(this.salesToday, this.sumPrices(yesterdayData))
    },

    formatCurrency(value) {
      return new Intl.NumberFormat("pt-BR", {
        style: "currency",
        currency: "BRL"
      }).format(value)
    },

    formatDate(date) {
      return new Date(date).toLocaleString()
    },

    channelClass(channel) {
      const map = {
        ble:  'px-2 py-0.5 rounded-full text-xs font-medium bg-blue-100 text-blue-700',
        mqtt: 'px-2 py-0.5 rounded-full text-xs font-medium bg-violet-100 text-violet-700',
        cash: 'px-2 py-0.5 rounded-full text-xs font-medium bg-gray-100 text-gray-600',
      }
      return map[channel] ?? 'px-2 py-0.5 rounded-full text-xs font-medium bg-gray-100 text-gray-500'
    }

  }

}
</script>
