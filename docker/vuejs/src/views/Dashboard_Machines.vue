<template>
  <div class="p-6">

    <h1 class="text-2xl font-bold mb-6">Machines</h1>

    <div class="bg-white rounded-xl shadow overflow-hidden">

      <table class="w-full text-sm">
        <thead class="bg-gray-100 text-gray-600">
          <tr>
            <th class="p-3 text-left">Machine</th>
            <th class="p-3 text-left">Status</th>
            <th class="p-3 text-left">Embedded</th>
            <th class="p-3 text-right">Actions</th>
          </tr>
        </thead>

        <tbody>
          <tr
            v-for="machine in machines"
            :key="machine.id"
            class="border-t"
          >
            <td class="p-3 font-medium">
              {{ machine.name }}
            </td>

            <td class="p-3">
              <span
                :class="machine.embedded?.status === 'online'
                ? 'text-green-600'
                : 'text-red-500'"
              >
                {{ machine.embedded?.status || 'No device' }}
              </span>
            </td>

            <td class="p-3">
              {{ machine.embedded ? 'Connected' : 'None' }}
            </td>

            <td class="p-3 text-right">

              <button
                v-if="machine.embedded?.status === 'online'"
                @click="openCreditModal(machine)"
                class="px-3 py-1 bg-green-600 text-white rounded hover:bg-green-700"
              >
                Send Credit
              </button>

              <span
                v-else
                class="text-gray-400"
              >
                unavailable
              </span>

            </td>

          </tr>
        </tbody>

      </table>

    </div>

    <!-- CREDIT MODAL -->

    <div
      v-if="showModal"
      class="fixed inset-0 bg-black/40 flex items-center justify-center"
    >

      <div class="bg-white p-6 rounded-xl w-80">

        <h2 class="text-lg font-semibold mb-4">
          Send Credit
        </h2>

        <p class="text-sm text-gray-600 mb-3">
          Machine: {{ selectedMachine?.name }}
        </p>

        <input
          v-model="creditAmount"
          type="number"
          step="0.01"
          placeholder="Amount"
          class="w-full border rounded p-2 mb-4"
        />

        <div class="flex justify-end gap-2">

          <button
            @click="showModal=false"
            class="px-3 py-1 border rounded"
          >
            Cancel
          </button>

          <button
            @click="sendCredit"
            class="px-3 py-1 bg-blue-600 text-white rounded"
          >
            Send
          </button>

        </div>

      </div>

    </div>

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'

export default {

  data() {
    return {
      machines: [],
      showModal: false,
      selectedMachine: null,
      creditAmount: null
    }
  },

  async mounted() {
    await this.loadMachines()
  },

  methods: {

    async loadMachines() {

      const { data, error } = await supabase
        .from('machines')
        .select(`*, embedded(status, status_at, subdomain)`)

      if (!error) this.machines = data
    },

    openCreditModal(machine) {
      this.selectedMachine = machine
      this.creditAmount = null
      this.showModal = true
    },

    async sendCredit() {

      const { data, error } = await supabase.functions.invoke( 'send-credit',
        {
          body: {
            subdomain: this.selectedMachine.embedded.subdomain,
            amount: parseFloat(this.creditAmount)
          }
        }
      )

      if (error) {
        console.error(error)
        alert("Failed to send credit")
        return
      }

      this.showModal = false
      this.creditAmount = null

      alert("Credit sent successfully")
    }
  }

}
</script>