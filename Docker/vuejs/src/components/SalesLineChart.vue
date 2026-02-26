<template>
  <canvas ref="chart"></canvas>
</template>

<script>
import {
  Chart,
  LineController,
  LineElement,
  PointElement,
  LinearScale,
  CategoryScale,
  Tooltip,
  Legend
} from 'chart.js'

Chart.register(
  LineController,
  LineElement,
  PointElement,
  LinearScale,
  CategoryScale,
  Tooltip,
  Legend
)

export default {
  name: 'SalesLineChart',

  props: {
    sales: {
      type: Array,
      required: true
    }
  },

  data() {
    return {
      chart: null
    }
  },

  watch: {
    sales: {
      deep: true,
      immediate: true,
      handler() {
        this.renderChart()
      }
    }
  },

  methods: {
    renderChart() {
      if (this.chart) {
        this.chart.destroy()
      }

      const grouped = {}

      this.sales.forEach(({ created_at }) => {
        const day = created_at.toISOString().slice(0, 10)
        grouped[day] = (grouped[day] || 0) + 1
      })

      const labels = Object.keys(grouped).sort()
      const data = labels.map(day => grouped[day])

      this.chart = new Chart(this.$refs.chart, {
        type: 'line',
        data: {
          labels,
          datasets: [
            {
              label: 'Vendas por dia',
              data,
              borderColor: '#2563eb',
              backgroundColor: 'rgba(37, 99, 235, 0.15)',
              tension: 0.3,
              fill: true
            }
          ]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          scales: {
            y: {
              beginAtZero: true,
              ticks: { stepSize: 1 }
            }
          }
        }
      })
    }
  }
}
</script>

<style scoped>
canvas {
  width: 100%;
  height: 300px;
}
</style>