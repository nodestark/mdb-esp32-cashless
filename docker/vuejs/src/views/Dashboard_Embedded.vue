<template>
  <div class="p-6 space-y-6">

    <!-- HEADER -->
    <div>
      <h1 class="text-2xl font-bold text-gray-800">Devices</h1>
      <p class="text-gray-500">Embedded devices connected to machines</p>
    </div>

    <!-- METRICS -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4">

      <div class="bg-white rounded-xl shadow p-4">
        <p class="text-gray-500 text-sm">Total Devices</p>
        <p class="text-2xl font-semibold">{{ devices.length }}</p>
      </div>

      <div class="bg-white rounded-xl shadow p-4">
        <p class="text-gray-500 text-sm">Online</p>
        <p class="text-2xl font-semibold text-green-600">{{ onlineDevices }}</p>
      </div>

      <div class="bg-white rounded-xl shadow p-4">
        <p class="text-gray-500 text-sm">Offline</p>
        <p class="text-2xl font-semibold text-red-600">{{ offlineDevices }}</p>
      </div>

    </div>

    <!-- TABLE -->
    <div class="bg-white rounded-xl shadow overflow-hidden">

      <table class="w-full text-sm">

        <thead class="bg-gray-50">
          <tr class="text-left text-gray-600">
            <th class="p-4">MAC</th>
            <th class="p-4">Machine</th>
            <th class="p-4">Subdomain</th>
            <th class="p-4">Status</th>
            <th class="p-4">Last Seen</th>
          </tr>
        </thead>

        <tbody>

          <tr
            v-for="device in devices"
            :key="device.id"
            class="border-t hover:bg-gray-50"
          >

            <td class="p-4 font-mono text-gray-700">
              {{ device.mac_address }}
            </td>

            <td class="p-4 text-gray-700">
              {{ device.machines?.name || '-' }}
            </td>

            <td class="p-4 text-gray-500">
              {{ device.subdomain }}
            </td>

            <td class="p-4">

              <span
                v-if="getStatus(device) === 'online'"
                class="px-2 py-1 text-xs font-semibold text-green-700 bg-green-100 rounded"
              >
                Online
              </span>

              <span
                v-else
                class="px-2 py-1 text-xs font-semibold text-red-700 bg-red-100 rounded"
              >
                Offline
              </span>

            </td>

            <td class="p-4 text-gray-500">
              {{ formatDate(device.status_at) }}
            </td>

          </tr>

        </tbody>

      </table>

      <!-- LOADING -->
      <div v-if="loading" class="p-6 text-center text-gray-500">
        Loading devices...
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

  name: "DashboardDevices",

  data() {
    return {
      devices: [],
      loading: false,
      error: null
    }
  },

  computed: {

    onlineDevices() {
      return this.devices.filter(d => this.getStatus(d) === "online").length
    },

    offlineDevices() {
      return this.devices.filter(d => this.getStatus(d) === "offline").length
    }

  },

  async mounted() {
    await this.loadDevices()
  },

  methods: {

    async loadDevices() {

      this.loading = true

      const { data, error } = await supabase
        .from("embedded")
        .select(`
          id,
          mac_address,
          subdomain,
          status,
          status_at,
          machines (
            name
          )
        `)

      if (error) {
        this.error = error.message
      } else {
        this.devices = data
      }

      this.loading = false
    },

    getStatus(device) {

      if (!device.status_at) return "offline"

      const last = new Date(device.status_at)
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