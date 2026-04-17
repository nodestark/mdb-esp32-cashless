<template>

<div class="p-6 space-y-6">

  <!-- HEADER -->
  <div class="flex justify-between items-center">
    <h1 class="text-2xl font-bold text-gray-800">Metrics</h1>
  </div>

  <!-- PERIOD FILTER -->
  <div class="flex items-center gap-3">
    <span class="text-sm text-gray-500">Period:</span>
    <select
      v-model="period"
      @change="loadMetrics"
      class="border rounded p-2 text-sm"
    >
      <option value="24h">Last 24h</option>
      <option value="7d">Last 7 days</option>
      <option value="30d">Last 30 days</option>
    </select>
  </div>

  <!-- PAX COUNTER -->
  <div class="bg-white rounded-xl shadow p-5">

    <div class="flex flex-wrap items-start justify-between gap-4 mb-4">
      <div>
        <h2 class="font-semibold text-gray-700">PAX Counter</h2>
        <p class="text-xs text-gray-400 mt-0.5">People detected near the machine</p>
      </div>

      <!-- MACHINE CHECKBOXES -->
      <div class="flex flex-wrap gap-2">
        <button
          @click="toggleAllPax"
          :class="paxAllSelected ? 'bg-slate-800 text-white' : 'bg-gray-100 text-gray-600 hover:bg-gray-200'"
          class="px-3 py-1 rounded-full text-xs font-medium transition"
        >
          All
        </button>
        <button
          v-for="machine in machines"
          :key="machine.id"
          @click="togglePaxMachine(machine.id)"
          :class="paxSelectedMachines.includes(machine.id) ? 'bg-indigo-600 text-white' : 'bg-gray-100 text-gray-600 hover:bg-gray-200'"
          class="px-3 py-1 rounded-full text-xs font-medium transition"
        >
          {{ machine.name }}
        </button>
      </div>
    </div>

    <apexchart
      type="area"
      height="280"
      :options="paxOptions"
      :series="paxSeries"
    />

  </div>

  <!-- SALES -->
  <div class="bg-white rounded-xl shadow p-5">

    <div class="flex flex-wrap items-start justify-between gap-4 mb-4">
      <div>
        <h2 class="font-semibold text-gray-700">Sales</h2>
        <p class="text-xs text-gray-400 mt-0.5">Revenue over time</p>
      </div>

      <select
        v-model="salesMachine"
        @change="loadSales"
        class="border rounded p-2 text-sm"
      >
        <option :value="null">All Machines</option>
        <option
          v-for="machine in machines"
          :key="machine.id"
          :value="machine.id"
        >
          {{ machine.name }}
        </option>
      </select>
    </div>

    <apexchart
      type="bar"
      height="280"
      :options="salesOptions"
      :series="salesSeries"
    />

  </div>

</div>

</template>

<script>

import { supabase } from "@/lib/supabase"
import VueApexCharts from "vue3-apexcharts"

const PAX_COLORS = ["#6366f1","#f59e0b","#ef4444","#10b981","#3b82f6","#ec4899","#8b5cf6","#14b8a6"]

export default {

  components: {
    apexchart: VueApexCharts
  },

  data() {
    return {
      machines: [],
      period: "24h",

      paxSelectedMachines: [],
      paxSeries: [],
      paxOptions: {
        chart: { id: "pax", toolbar: { show: true } },
        xaxis: { type: "datetime" },
        stroke: { curve: "smooth", width: 2 },
        fill: { type: "gradient", gradient: { opacityFrom: 0.4, opacityTo: 0 } },
        legend: { show: true, position: "top" },
        tooltip: { x: { format: "dd/MM HH:mm" } },
        colors: PAX_COLORS,
        noData: { text: "No data available" }
      },

      salesMachine: null,
      salesSeries: [],
      salesOptions: {
        chart: { id: "sales", toolbar: { show: true } },
        xaxis: { type: "datetime" },
        yaxis: { labels: { formatter: v => "R$ " + (v ?? 0).toFixed(2) } },
        legend: { show: false },
        tooltip: { x: { format: "dd/MM HH:mm" }, y: { formatter: v => "R$ " + (v ?? 0).toFixed(2) } },
        colors: ["#10b981"],
        noData: { text: "No data available" }
      }
    }
  },

  computed: {
    paxAllSelected() {
      return this.paxSelectedMachines.length === 0
    }
  },

  async mounted() {
    await this.loadMachines()
    await this.loadMetrics()
  },

  methods: {

    async loadMachines() {
      const { data, error } = await supabase
        .from("machines")
        .select("id, name")
        .order("name")

      if (error) { console.error(error); return }
      this.machines = data
    },

    getPeriodStart() {
      const now = new Date()
      if (this.period === "24h") now.setHours(now.getHours() - 24)
      if (this.period === "7d")  now.setDate(now.getDate() - 7)
      if (this.period === "30d") now.setDate(now.getDate() - 30)
      return now.toISOString()
    },

    toggleAllPax() {
      this.paxSelectedMachines = []
      this.loadPax()
    },

    togglePaxMachine(id) {
      const idx = this.paxSelectedMachines.indexOf(id)
      if (idx === -1) this.paxSelectedMachines.push(id)
      else this.paxSelectedMachines.splice(idx, 1)
      this.loadPax()
    },

    async loadMetrics() {
      await Promise.all([this.loadPax(), this.loadSales()])
    },

    async loadPax() {
      const start = this.getPeriodStart()
      const targetMachines = this.paxSelectedMachines.length > 0
        ? this.paxSelectedMachines
        : this.machines.map(m => m.id)

      const results = await Promise.all(
        targetMachines.map(id =>
          supabase
            .from("metrics")
            .select("value, created_at")
            .eq("name", "paxcounter")
            .eq("machine_id", id)
            .gte("created_at", start)
            .order("created_at")
        )
      )

      this.paxSeries = targetMachines.map((id, i) => {
        const machine = this.machines.find(m => m.id === id)
        const data = (results[i].data ?? []).map(m => ({ x: m.created_at, y: m.value }))
        return { name: machine?.name ?? id, data }
      })
    },

    async loadSales() {
      const start = this.getPeriodStart()

      let query = supabase
        .from("sales_metrics_v1")
        .select("created_at, total")
        .gte("created_at", start)
        .order("created_at")

      if (this.salesMachine) query = query.eq("machine_id", this.salesMachine)

      const { data, error } = await query
      if (error) { console.error(error); return }

      this.salesSeries = [{
        name: "Sales",
        data: (data ?? []).map(s => ({ x: s.created_at, y: s.total }))
      }]
    }

  }

}

</script>
