<template>
  <div class="p-6 space-y-6">

    <!-- HEADER -->
    <div>
      <h1 class="text-2xl font-bold text-gray-800">Machines</h1>
      <p class="text-gray-500">Manage and monitor vending machines</p>
    </div>

    <!-- METRICS -->
    <div class="grid grid-cols-1 md:grid-cols-4 gap-4">

      <div class="bg-white rounded-xl shadow p-4">
        <p class="text-gray-500 text-sm">Total Machines</p>
        <p class="text-2xl font-semibold">{{ machines.length }}</p>
      </div>

      <div class="bg-white rounded-xl shadow p-4">
        <p class="text-gray-500 text-sm">Online</p>
        <p class="text-2xl font-semibold text-green-600">{{ onlineMachines }}</p>
      </div>

      <div class="bg-white rounded-xl shadow p-4">
        <p class="text-gray-500 text-sm">Offline</p>
        <p class="text-2xl font-semibold text-red-600">{{ offlineMachines }}</p>
      </div>

      <div class="bg-white rounded-xl shadow p-4">
        <p class="text-gray-500 text-sm">No Device</p>
        <p class="text-2xl font-semibold text-gray-500">{{ noDeviceMachines }}</p>
      </div>

    </div>

    <!-- TABLE -->
    <div class="bg-white rounded-xl shadow overflow-hidden">

      <table class="w-full text-sm">

        <thead class="bg-gray-50">
          <tr class="text-left text-gray-600">
            <th class="p-4">Machine</th>
            <th class="p-4">Serial</th>
            <th class="p-4">Status</th>
            <th class="p-4">Last Seen</th>
          </tr>
        </thead>

        <tbody>

          <tr
            v-for="machine in machines"
            :key="machine.id"
            class="border-t hover:bg-gray-50 cursor-pointer"
          >

            <td class="p-4 font-medium text-gray-800">
              {{ machine.name }}
            </td>

            <td class="p-4 text-gray-500">
              {{ machine.serial_number }}
            </td>

            <td class="p-4">

              <span
                v-if="getStatus(machine) === 'online'"
                class="px-2 py-1 text-xs font-semibold text-green-700 bg-green-100 rounded"
              >
                Online
              </span>

              <span
                v-else-if="getStatus(machine) === 'offline'"
                class="px-2 py-1 text-xs font-semibold text-red-700 bg-red-100 rounded"
              >
                Offline
              </span>

              <span
                v-else
                class="px-2 py-1 text-xs font-semibold text-gray-700 bg-gray-200 rounded"
              >
                No Device
              </span>

            </td>

            <td class="p-4 text-gray-500">
              {{ formatDate(machine.embedded?.status_at) }}
            </td>

          </tr>

        </tbody>

      </table>

      <!-- LOADING -->
      <div v-if="loading" class="p-6 text-center text-gray-500">
        Loading machines...
      </div>

      <!-- ERROR -->
      <div v-if="error" class="p-6 text-red-500">
        {{ error }}
      </div>

    </div>

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'

export default {

  name: "DashboardMachines",

  data() {
    return {
      machines: [],
      loading: false,
      error: null
    }
  },

  computed: {

    onlineMachines() {
      return this.machines.filter(m => this.getStatus(m) === "online").length
    },

    offlineMachines() {
      return this.machines.filter(m => this.getStatus(m) === "offline").length
    },

    noDeviceMachines() {
      return this.machines.filter(m => this.getStatus(m) === "no-device").length
    }

  },

  async mounted() {
    await this.loadMachines()
  },

  methods: {

    async loadMachines() {

      this.loading = true

      const { data, error } = await supabase
        .from("machines")
        .select(`
          id,
          name,
          serial_number,
          embedded (
            status,
            status_at
          )
        `)

      if (error) {
        this.error = error.message
      } else {
        this.machines = data
      }

      this.loading = false
    },

    getStatus(machine) {

      if (!machine.embedded) return "no-device"

      const last = new Date(machine.embedded.status_at)
      const now = new Date()

      const diff = (now - last) / 1000

      if (diff < 120) return "online"

      return "offline"
    },

    formatDate(date) {

      if (!date) return "-"

      return new Date(date).toLocaleString()
    }

  }

}
</script>