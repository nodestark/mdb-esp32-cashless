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
        <th class="p-3">Model</th>
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
          <span v-if="machine.machine_models">
            {{ machine.machine_models.name }}
          </span>
          <span v-else class="text-gray-400">
            No model
          </span>
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
            class="px-3 py-1 bg-slate-700 hover:bg-slate-600 text-white text-sm rounded transition"
          >
            Link Device
          </button>

          <button
            :disabled="!machine.embedded"
            @click="openCreditModal(machine)"
            class="px-3 py-1 bg-green-600 hover:bg-green-500 text-white text-sm rounded transition disabled:opacity-40"
          >
            Send Credit
          </button>

          <button
            @click="openModelDialog(machine)"
            class="px-3 py-1 border border-slate-400 hover:bg-slate-100 text-slate-700 text-sm rounded transition"
          >
            Link Model
          </button>

          <button
            @click="openCoilsModal(machine)"
            class="px-3 py-1 border border-blue-400 hover:bg-blue-50 text-blue-700 text-sm rounded transition"
          >
            Coils
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
      Link Device
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

<!-- LINK MODEL MODAL -->

<div
  v-if="showModelDialog"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>
  <div class="bg-white p-6 rounded-xl w-96">

    <h2 class="text-lg font-semibold mb-4">
      Link Model
    </h2>

    <select
      v-model="selectedModelId"
      class="w-full border rounded p-2 mb-4"
    >
      <option disabled value="">Select a model</option>
      <option
        v-for="model in machineModels"
        :key="model.id"
        :value="model.id"
      >
        {{ model.name }}
      </option>
    </select>

    <div class="flex justify-end gap-2">
      <button
        @click="showModelDialog = false"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="linkModel"
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

<!-- COILS MODAL -->

<div
  v-if="showCoilsModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center z-50"
>
  <div class="bg-white rounded-xl w-full max-w-3xl mx-4 flex flex-col max-h-[80vh]">

    <!-- HEADER -->
    <div class="p-5 border-b flex justify-between items-center">
      <div>
        <h2 class="text-lg font-semibold text-gray-800">Coils</h2>
        <p class="text-sm text-gray-500">{{ selectedMachine?.name }}</p>
      </div>
      <button @click="showCoilsModal = false" class="text-gray-400 hover:text-gray-600 text-xl leading-none">&times;</button>
    </div>

    <!-- BODY -->
    <div class="overflow-y-auto flex-1">

      <!-- LOADING -->
      <div v-if="loadingCoils" class="p-8 text-center text-gray-500 text-sm">
        Loading coils...
      </div>

      <!-- EMPTY -->
      <div v-else-if="coils.length === 0" class="p-8 text-center text-gray-400 text-sm">
        No coils configured for this machine.<br>Link a model first to generate coils automatically.
      </div>

      <!-- TABLE -->
      <table v-else class="w-full text-sm">

        <thead class="bg-gray-50 sticky top-0">
          <tr class="text-left text-gray-600">
            <th class="p-4 w-12">#</th>
            <th class="p-4">Slot</th>
            <th class="p-4">Product</th>
            <th class="p-4 w-32">Price</th>
            <th class="p-4 w-24 text-center">Capacity</th>
          </tr>
        </thead>

        <tbody>
          <tr
            v-for="coil in coils"
            :key="coil.id"
            class="border-t hover:bg-gray-50"
          >

            <td class="p-4 text-gray-400 font-mono">
              {{ coil.item_number ?? '-' }}
            </td>

            <td class="p-4 font-medium text-gray-700">
              {{ coil.alias || '-' }}
            </td>

            <td class="p-4">
              <select
                v-model="coil.selectedProductId"
                @change="onProductChange(coil)"
                class="w-full border rounded px-2 py-1 text-sm text-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-300"
              >
                <option :value="null">— unassigned —</option>
                <option
                  v-for="product in allProducts"
                  :key="product.id"
                  :value="product.id"
                >
                  {{ product.name }}
                </option>
              </select>
            </td>

            <td class="p-4">
              <input
                v-model.number="coil.selectedItemPrice"
                type="number"
                step="0.01"
                min="0"
                placeholder="Price"
                class="w-full border rounded px-2 py-1 text-sm text-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-300"
              />
            </td>

            <td class="p-4 text-gray-500 text-center">
              {{ coil.capacity ?? '-' }}
            </td>

          </tr>
        </tbody>

      </table>

    </div>

    <!-- FOOTER -->
    <div class="p-4 border-t flex justify-end gap-2">
      <button
        @click="showCoilsModal = false"
        class="px-4 py-2 border rounded text-sm text-gray-600 hover:bg-gray-50 transition"
      >
        Cancel
      </button>
      <button
        :disabled="savingCoils || coils.length === 0"
        @click="saveCoils"
        class="px-4 py-2 bg-blue-600 hover:bg-blue-500 text-white text-sm rounded transition disabled:opacity-40"
      >
        {{ savingCoils ? 'Saving...' : 'Save' }}
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

      creditAmount: null,

      showModelDialog: false,
      selectedModelId: null,
      machineModels: [],

      showCoilsModal: false,
      loadingCoils: false,
      savingCoils: false,
      coils: [],
      allProducts: []
    }
  },

  mounted() {
    this.loadMachines()
  },

  methods: {

    async loadMachines() {
      const { data, error } = await supabase
        .from("machines")
        .select(`*, machine_models(name), embedded(id, subdomain, status)`)

      if (error) {
        console.error("Failed to load machines:", error)
        return
      }

      this.machines = data
    },

    async createMachine() {
      if (!this.newMachineName) return

      const { error } = await supabase
        .from("machines")
        .insert({ name: this.newMachineName })

      if (error) {
        console.error("Failed to create machine:", error)
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
        console.error("Failed to load available devices:", error)
        return
      }

      this.availableEmbeddeds = data
    },

    async linkEmbedded() {
      if (!this.selectedEmbedded) return

      const { error } = await supabase.rpc("bind_embedded_machine", {
        embedded_id_: this.selectedEmbedded.id,
        machine_id_: this.selectedMachine.id
      })

      if (error) {
        console.error("Failed to link device:", error)
        return
      }

      this.showLinkModal = false
      this.selectedEmbedded = null
      await this.loadMachines()
    },

    openCreditModal(machine) {
      this.selectedMachine = machine
      this.creditAmount = null
      this.showCreditModal = true
    },

    async sendCredit() {
      const { error } = await supabase.functions.invoke("send-credit", {
        body: {
          subdomain: this.selectedMachine.embedded.subdomain,
          amount: parseFloat(this.creditAmount)
        }
      })

      if (error) {
        console.error("Failed to send credit:", error)
        return
      }

      this.showCreditModal = false
    },

    async openModelDialog(machine) {
      this.selectedMachine = machine
      this.selectedModelId = machine.model_id || null

      if (!this.machineModels.length) {
        await this.fetchModels()
      }

      this.showModelDialog = true
    },

    async fetchModels() {
      const { data, error } = await supabase
        .from("machine_models")
        .select("*")
        .order("name", { ascending: true })

      if (error) {
        console.error("Failed to load models:", error)
        return
      }

      this.machineModels = data
    },

    async linkModel() {
      if (!this.selectedMachine || !this.selectedModelId) return

      const { error } = await supabase
        .from("machines")
        .update({ model_id: this.selectedModelId })
        .eq("id", this.selectedMachine.id)

      if (error) {
        console.error("Failed to link model:", error)
        return
      }

      this.selectedMachine.model_id = this.selectedModelId
      this.showModelDialog = false
    },

    async openCoilsModal(machine) {
      this.selectedMachine = machine
      this.coils = []
      this.showCoilsModal = true

      if (this.allProducts.length === 0) {
        await this.fetchAllProducts()
      }
      await this.loadCoils(machine.id)
    },

    async loadCoils(machineId) {
      this.loadingCoils = true

      const { data, error } = await supabase
        .from("machine_coils")
        .select("id, item_number, alias, capacity, product_id, item_price")
        .eq("machine_id", machineId)
        .order("item_number")

      if (error) {
        console.error("Failed to load coils:", error)
      } else {
        this.coils = data.map(c => {
          const product = c.product_id
            ? this.allProducts.find(p => p.id === c.product_id)
            : null
          return {
            ...c,
            selectedProductId: c.product_id ?? null,
            selectedItemPrice: c.item_price ?? product?.price ?? null
          }
        })
      }

      this.loadingCoils = false
    },

    async fetchAllProducts() {
      const { data, error } = await supabase
        .from("products")
        .select("id, name, price")
        .eq("enabled", true)
        .order("name")

      if (!error) this.allProducts = data ?? []
    },

    onProductChange(coil) {
      const product = this.allProducts.find(p => p.id === coil.selectedProductId)
      coil.selectedItemPrice = product?.price ?? null
    },

    async saveCoils() {
      this.savingCoils = true

      try {
        for (const coil of this.coils) {
          const { data: updated, error } = await supabase
            .from("machine_coils")
            .update({
              product_id: coil.selectedProductId ?? null,
              item_price: coil.selectedItemPrice ?? null
            })
            .eq("id", coil.id)
            .select("id")

          if (error) throw error
          if (!updated?.length) throw new Error(`Failed to update slot "${coil.alias || coil.id}"`)
        }
        this.showCoilsModal = false
      } catch (err) {
        console.error("Failed to save coils:", err)
        alert("Failed to save coils: " + err.message)
      } finally {
        this.savingCoils = false
      }
    }

  }

}

</script>
