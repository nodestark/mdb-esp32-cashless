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

    <!-- AI DIAGNOSIS -->
    <div class="bg-white rounded-xl shadow p-5">

      <div class="flex items-center justify-between mb-4">
        <div>
          <h2 class="font-semibold text-gray-700">AI Operation Diagnosis</h2>
          <p class="text-xs text-gray-400 mt-0.5">Analysis of all machines in the last 30 days</p>
        </div>
        <button
          @click="generateDiagnosis"
          :disabled="diagnosisLoading"
          class="flex items-center gap-1.5 px-3 py-1.5 rounded-full text-sm font-medium bg-purple-100 text-purple-700 hover:bg-purple-200 transition disabled:opacity-50"
        >
          <svg class="w-4 h-4" :class="{ 'animate-spin': diagnosisLoading }" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
            <path v-if="!diagnosisLoading" stroke-linecap="round" stroke-linejoin="round" d="M9.813 15.904L9 18.75l-.813-2.846a4.5 4.5 0 00-3.09-3.09L2.25 12l2.846-.813a4.5 4.5 0 003.09-3.09L9 5.25l.813 2.846a4.5 4.5 0 003.09 3.09L15.75 12l-2.846.813a4.5 4.5 0 00-3.09 3.09z"/>
            <path v-else stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0l3.181 3.183a8.25 8.25 0 0013.803-3.7M4.031 9.865a8.25 8.25 0 0113.803-3.7l3.181 3.182m0-4.991v4.99"/>
          </svg>
          {{ diagnosisLoading ? 'Analyzing...' : diagnosisText ? 'Refresh' : 'Generate Diagnosis' }}
        </button>
      </div>

      <div v-if="!diagnosisText && !diagnosisLoading && !diagnosisError" class="text-center py-8 text-gray-400 text-sm">
        Click "Generate Diagnosis" to get an AI analysis of your operation.
      </div>

      <div v-if="diagnosisLoading" class="text-sm text-gray-500 py-4 text-center">
        Collecting machine data and generating insights...
      </div>

      <p v-if="diagnosisError" class="text-sm text-red-500">{{ diagnosisError }}</p>

      <p v-if="diagnosisText" class="text-sm text-gray-700 leading-relaxed whitespace-pre-wrap">{{ diagnosisText }}</p>

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
      todayVariation: null,

      diagnosisText: '',
      diagnosisLoading: false,
      diagnosisError: '',
      diagnosisAbort: null
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

    async generateDiagnosis() {
      if (this.diagnosisAbort) this.diagnosisAbort.abort()

      this.diagnosisLoading = true
      this.diagnosisError = ''
      this.diagnosisText = ''

      const abort = new AbortController()
      this.diagnosisAbort = abort

      try {
        const { data: cred } = await supabase
          .from('credentials')
          .select('value')
          .eq('key', 'openai_api_key')
          .maybeSingle()

        if (!cred?.value) throw new Error('OpenAI API key not configured. Add it in Settings → API Keys.')

        const since = new Date()
        since.setDate(since.getDate() - 30)

        const [machinesRes, salesRes] = await Promise.all([
          supabase
            .from('machines')
            .select('id, name, category, monthly_rent, machine_coils(capacity, current_stock), embedded(status)'),
          supabase
            .from('sales')
            .select('machine_id, item_price')
            .gte('created_at', since.toISOString())
        ])

        const machines = machinesRes.data ?? []
        const sales = salesRes.data ?? []

        const machineStats = machines.map(m => {
          const mSales = sales.filter(s => s.machine_id === m.id)
          const revenue = mSales.reduce((s, r) => s + (r.item_price ?? 0), 0)
          const count = mSales.length
          const ticket = count > 0 ? revenue / count : 0
          const coils = m.machine_coils ?? []
          const totalStock = coils.reduce((s, c) => s + (c.current_stock ?? 0), 0)
          const totalCap = coils.reduce((s, c) => s + (c.capacity ?? 0), 0)
          const stockPct = totalCap > 0 ? Math.round((totalStock / totalCap) * 100) : null
          const profit = m.monthly_rent ? revenue - m.monthly_rent : null

          return {
            name: m.name,
            category: m.category ?? 'N/A',
            status: m.embedded?.status ?? 'unknown',
            monthly_rent: m.monthly_rent ?? null,
            revenue: revenue.toFixed(2),
            sales_count: count,
            avg_ticket: ticket.toFixed(2),
            stock_pct: stockPct !== null ? stockPct + '%' : 'N/A',
            net_profit: profit !== null ? profit.toFixed(2) : 'N/A'
          }
        })

        const totalRevenue = sales.reduce((s, r) => s + (r.item_price ?? 0), 0)
        const totalSalesCount = sales.length
        const globalTicket = totalSalesCount > 0 ? totalRevenue / totalSalesCount : 0
        const onlineCount = machines.filter(m => m.embedded?.status === 'online').length

        const prompt = `You are a vending machine business analyst. Analyze the operational data below and provide a complete diagnosis in Portuguese (Brazil), with practical and actionable insights.

GENERAL OVERVIEW (last 30 days):
- Total machines: ${machines.length} (${onlineCount} online)
- Total revenue: R$ ${totalRevenue.toFixed(2)}
- Total transactions: ${totalSalesCount}
- Global average ticket: R$ ${globalTicket.toFixed(2)}

PERFORMANCE PER MACHINE:
${machineStats.map(m => `
  Machine: ${m.name}
  - Category: ${m.category}
  - Status: ${m.status}
  - Revenue: R$ ${m.revenue}
  - Transactions: ${m.sales_count}
  - Average ticket: R$ ${m.avg_ticket}
  - Stock level: ${m.stock_pct}
  - Monthly rent: ${m.monthly_rent ? 'R$ ' + m.monthly_rent : 'not defined'}
  - Net profit (revenue - rent): ${m.net_profit !== 'N/A' ? 'R$ ' + m.net_profit : 'N/A'}
`).join('')}

Provide:
1. Overall assessment of the operation
2. Best and worst performing machines and why
3. Machines with risk (low stock, offline, negative ROI)
4. 3 to 5 prioritized recommendations to improve results`

        const res = await fetch('https://api.openai.com/v1/chat/completions', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${cred.value}`
          },
          body: JSON.stringify({
            model: 'gpt-4o-mini',
            messages: [{ role: 'user', content: prompt }],
            temperature: 0.7
          }),
          signal: abort.signal
        })

        if (!res.ok) {
          const err = await res.json()
          throw new Error(err.error?.message ?? 'OpenAI request failed')
        }

        const json = await res.json()
        this.diagnosisText = json.choices[0].message.content

      } catch (err) {
        if (err.name === 'AbortError') return
        this.diagnosisError = err.message
        console.error('generateDiagnosis error:', err)
      } finally {
        this.diagnosisAbort = null
        this.diagnosisLoading = false
      }
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
