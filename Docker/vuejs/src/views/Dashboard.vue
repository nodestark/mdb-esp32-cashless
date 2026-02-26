<template>
  <main class="content">
    <!-- KPIs -->
    <div class="kpis">
      <div class="card">
        <span>Hoje</span>
        <strong>R$ {{ totals.day.toFixed(2) }}</strong>
      </div>

      <div class="card">
        <span>Semana</span>
        <strong>R$ {{ totals.week.toFixed(2) }}</strong>
      </div>

      <div class="card">
        <span>Mês</span>
        <strong>R$ {{ totals.month.toFixed(2) }}</strong>
      </div>
    </div>

    <!-- Chart -->
    <div class="chart-card">
      <SalesLineChart :sales="sales" />
    </div>
  </main>
</template>

<script>
import SalesLineChart from '../components/SalesLineChart.vue'

export default {
  name: 'Dashboard',
  components: { SalesLineChart },

  data() {
    return {
      sales: [],
      totals: {
        day: 0,
        week: 0,
        month: 0
      }
    }
  },

  mounted() {
    this.generateFakeSales()
  },

  watch: {
    sales: {
      deep: true,
      immediate: true,
      handler() {
        this.calculateTotals()
      }
    }
  },

  methods: {
    generateFakeSales() {
      const now = new Date()

      this.sales = Array.from({ length: 200 }, () => {
        const daysAgo = Math.floor(Math.random() * 30)
        const date = new Date(now)
        date.setDate(now.getDate() - daysAgo)

        return {
          amount: Math.random() * 50 + 5,
          created_at: date
        }
      })
    },

    calculateTotals() {
      this.totals.day = 0
      this.totals.week = 0
      this.totals.month = 0

      const now = new Date()

      const startOfDay = new Date(now)
      startOfDay.setHours(0, 0, 0, 0)

      const startOfWeek = new Date(now)
      startOfWeek.setDate(now.getDate() - now.getDay())
      startOfWeek.setHours(0, 0, 0, 0)

      const startOfMonth = new Date(now.getFullYear(), now.getMonth(), 1)

      this.sales.forEach(({ created_at, amount }) => {
        if (created_at >= startOfDay) this.totals.day += amount
        if (created_at >= startOfWeek) this.totals.week += amount
        if (created_at >= startOfMonth) this.totals.month += amount
      })
    }
  }
}
</script>