<template>
<div class="p-6">

  <!-- HEADER -->

  <div class="flex justify-between items-center mb-6">
    <h1 class="text-2xl font-bold">Machines</h1>

    <button
      @click="showAddModal = true"
      class="px-4 py-2 bg-slate-800 text-white rounded hover:bg-slate-700"
    >
      + Add Machine
    </button>
  </div>

  <!-- TABLE -->

  <table class="w-full border rounded-xl overflow-hidden">

    <thead class="bg-gray-100 text-left">
      <tr>
        <th class="p-3">Name</th>
        <th class="p-3">Embedded</th>
        <th class="p-3">Status</th>
        <th class="p-3">Actions</th>
      </tr>
    </thead>

    <tbody>

      <tr
        v-for="machine in machines"
        :key="machine.id"
        class="border-t"
      >

        <td class="p-3">
          {{ machine.name }}
        </td>

        <td class="p-3">

          <span v-if="machine.embedded">
            {{ machine.embedded.subdomain }}
          </span>

          <span v-else class="text-gray-400">
            Not linked
          </span>

        </td>

        <td class="p-3">
          <span
            v-if="machine.embedded"
            :class="machine.embedded.status === 'online'
              ? 'text-green-600'
              : 'text-red-600 font-bold'"
          >
            {{ machine.embedded.status }}
          </span>
        </td>

        <td class="p-3 flex gap-2">

          <button
            @click="openLinkModal(machine)"
            class="px-3 py-1 bg-blue-600 text-white rounded"
          >
            Link Embedded
          </button>
          <button
            :disabled="!machine.embedded"
            @click="openCreditModal(machine)"
            class="px-3 py-1 bg-green-600 text-white rounded disabled:opacity-40"
          >
            Send Credit
          </button>
        </td>

      </tr>

    </tbody>

  </table>

</div>

<!-- ADD MACHINE MODAL -->

<div
  v-if="showAddModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>

  <div class="bg-white p-6 rounded-xl w-80">

    <h2 class="text-lg font-semibold mb-4">
      Add Machine
    </h2>

    <input
      v-model="newMachineName"
      placeholder="Machine name"
      class="w-full border rounded p-2 mb-4"
      @keyup.enter="createMachine"
    />

    <div class="flex justify-end gap-2">

      <button
        @click="showAddModal=false"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="createMachine"
        class="px-3 py-1 bg-blue-600 text-white rounded"
      >
        Create
      </button>

    </div>

  </div>

</div>

<!-- LINK EMBEDDED MODAL -->

<div
  v-if="showLinkModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>

  <div class="bg-white p-6 rounded-xl w-96">

    <h2 class="text-lg font-semibold mb-4">
      Link Embedded
    </h2>

    <select
      v-model="selectedEmbedded"
      class="w-full border rounded p-2 mb-4"
    >

      <option
        v-for="embedded in availableEmbeddeds"
        :key="embedded.id"
        :value="embedded"
      >
        {{ embedded.subdomain }}
      </option>

    </select>

    <div class="flex justify-end gap-2">

      <button
        @click="showLinkModal=false"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="linkEmbedded"
        class="px-3 py-1 bg-green-600 text-white rounded"
      >
        Link
      </button>

    </div>

  </div>

</div>

<!-- SEND CREDIT MODAL -->

<div
  v-if="showCreditModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>

  <div class="bg-white p-6 rounded-xl w-80">

    <h2 class="text-lg font-semibold mb-4">
      Send Credit
    </h2>

    <p class="text-sm text-gray-500 mb-2">
      {{ selectedMachine?.embedded?.subdomain }}
    </p>

    <input
      v-model="creditAmount"
      type="number"
      placeholder="Amount"
      class="w-full border rounded p-2 mb-4"
    />

    <div class="flex justify-end gap-2">

      <button
        @click="showCreditModal=false"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="sendCredit"
        class="px-3 py-1 bg-green-600 text-white rounded"
      >
        Send
      </button>

    </div>

  </div>

</div>

</template>

<script>

import { supabase } from "@/lib/supabase"

export default {

  data() {
    return {

      machines: [],
      availableEmbeddeds: [],

      showAddModal: false,
      showLinkModal: false,
      showCreditModal: false,

      newMachineName: "",

      selectedMachine: null,
      selectedEmbedded: null,

      creditAmount: null

    }
  },

  mounted() {
    this.loadMachines()
  },

  methods: {

    async loadMachines() {

      const { data, error } = await supabase
        .from("machines")
        .select(`
          *,
          embedded (
            id,
            subdomain,
            status
          )
        `)

      if (error) {
        console.error(error)
        return
      }

      this.machines = data

    },

    async createMachine() {

      if (!this.newMachineName) {
        alert("Machine name required")
        return
      }

      const { error } = await supabase
        .from("machines")
        .insert({
          name: this.newMachineName
        })

      if (error) {
        console.error(error)
        alert("Error creating machine")
        return
      }

      this.newMachineName = ""
      this.showAddModal = false

      await this.loadMachines()

    },

    async openLinkModal(machine) {

      this.selectedMachine = machine
      this.showLinkModal = true

      const { data, error } = await supabase
        .from("embedded")
        .select("*")
        .is("machine_id", null)

      if (error) {
        console.error(error)
        return
      }

      this.availableEmbeddeds = data

    },

    async linkEmbedded() {

      if (!this.selectedEmbedded) {
        alert("Select an embedded")
        return
      }

      const { error } = await supabase.rpc("bind_embedded_machine", {
          embedded_id_: this.selectedEmbedded.id,
          machine_id_: this.selectedMachine.id
      })

      if (error) {
        console.error(error)
        alert("Failed to link embedded")
        return
      }
      this.showLinkModal = false
      this.selectedEmbedded = null

      await this.loadMachines()
    },

    openCreditModal(machine) {

      if (!machine.embedded) {
        alert("Machine has no embedded linked")
        return
      }

      this.selectedMachine = machine
      this.creditAmount = null
      this.showCreditModal = true

    },

    async sendCredit() {

      const subdomain = this.selectedMachine.embedded.subdomain

      const { error } = await supabase.functions.invoke(
        "send-credit",
        {
          body: {
            subdomain: subdomain,
            amount: parseFloat(this.creditAmount)
          }
        }
      )

      if (error) {
        console.error(error)
        alert("Failed to send credit")
        return
      }

      this.showCreditModal = false
      alert("Credit sent")

    }

  }

}

</script>