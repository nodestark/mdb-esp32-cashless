<template>

<div class="p-6">

  <!-- HEADER -->

  <div class="flex justify-between items-center mb-6">
    <h1 class="text-2xl font-bold">Metrics</h1>
  </div>

  <!-- FILTERS -->

  <div class="flex gap-4 mb-6">

    <!-- MACHINE -->

    <select
      v-model="selectedMachine"
      @change="loadMetrics"
      class="border rounded p-2"
    >
      <option
        v-for="machine in machines"
        :key="machine.id"
        :value="machine.id"
      >
        {{ machine.name }}
      </option>
    </select>

    <!-- PERIOD -->

    <select
      v-model="period"
      @change="loadMetrics"
      class="border rounded p-2"
    >
      <option value="24h">Last 24h</option>
      <option value="7d">Last 7 days</option>
      <option value="30d">Last 30 days</option>
    </select>

  </div>

  <!-- CHART -->

  <div class="bg-white p-4 rounded-xl shadow">

    <apexchart
      type="line"
      height="350"
      :options="chartOptions"
      :series="series"
    />

  </div>

</div>

</template>

<script>

import { supabase } from "@/lib/supabase"
import VueApexCharts from "vue3-apexcharts"

export default {

  components: {
    apexchart: VueApexCharts
  },

  data() {
    return {

      machines: [],
      selectedMachine: null,

      period: "24h",

      series: [
        {
          name: "PAX Counter",
          data: []
        },
        {
          name: "Sales",
          data: []
        }

      ],

      chartOptions: {

        chart: {
          id: "paxcounter"
        },

        xaxis: {
          type: "datetime"
        },

        stroke: {
          curve: "smooth"
        }

      }

    }
  },

  async mounted() {

    await this.loadMachines()

    if (this.machines.length) {
      this.selectedMachine = this.machines[0].id
      await this.loadMetrics()
    }

  },

  methods: {

    async loadMachines() {

      const { data, error } = await supabase
        .from("machines")
        .select("id, name")
        .order("name")

      if (error) {
        console.error(error)
        return
      }

      this.machines = data

    },

    getPeriodStart() {

      const now = new Date()

      if (this.period === "24h") {
        now.setHours(now.getHours() - 24)
      }

      if (this.period === "7d") {
        now.setDate(now.getDate() - 7)
      }

      if (this.period === "30d") {
        now.setDate(now.getDate() - 30)
      }

      return now.toISOString()

    },

    async loadMetrics() {

      if (!this.selectedMachine) return

      const start = this.getPeriodStart()

        /* ---------------------------
        PAX COUNTER
        --------------------------- */

      const { data, error } = await supabase
        .from("metrics")
        .select("value, created_at")
        .eq("machine_id", this.selectedMachine)
        .eq("name", "paxcounter")
        .gte("created_at", start)
        .order("created_at")

      if (error) {
        console.error(error)
        return
      }

      const paxPoints = data.map(m => ({
        x: m.created_at,
        y: m.value
      }))


      /* ---------------------------
         SALES
      --------------------------- */
      const { data: sales, error: salesError } = await supabase
      .from("sales_metrics_v1")
      .select("created_at, total")
      .eq("machine_id", this.selectedMachine)
      .gte("created_at", start)
      .order("created_at")

      if (salesError) {
        console.error(salesError)
        return
      }

      const salesPoints = sales.map(s => ({
        x: s.created_at,
        y: s.total
      }))

      this.series = [
        {
          name: "PAX Counter",
          data: paxPoints
        },
        {
          name: "Sales",
          data: salesPoints
        }
      ]

    }

  }

}

</script>