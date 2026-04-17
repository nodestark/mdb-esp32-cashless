<template>

<!-- TOAST -->
<transition name="fade">
  <div
    v-if="toast"
    class="fixed bottom-6 right-6 z-50 px-4 py-3 bg-slate-800 text-white text-sm rounded-xl shadow-lg"
  >
    {{ toast }}
  </div>
</transition>

<div class="p-6 space-y-6">

  <!-- HEADER -->
  <div class="flex justify-between items-center">
    <div>
      <h1 class="text-2xl font-bold text-gray-800">Machines</h1>
      <p class="text-gray-500">Manage your vending machines</p>
    </div>

    <button
      @click="showAddModal = true; createError = null"
      class="px-4 py-2 bg-slate-800 text-white rounded hover:bg-slate-700 transition"
    >
      + Add Machine
    </button>
  </div>

  <!-- TABLE -->
  <div class="bg-white rounded-xl shadow overflow-hidden">

    <table class="w-full text-sm">

      <thead class="bg-gray-50">
        <tr class="text-left text-gray-600">
          <th class="p-4">Name</th>
          <th class="p-4">Model</th>
          <th class="p-4">Device</th>
          <th class="p-4">Status</th>
          <th class="p-4 w-40">Stock</th>
          <th class="p-4">Actions</th>
        </tr>
      </thead>

      <tbody>

        <tr
          v-for="machine in machines"
          :key="machine.id"
          class="border-t hover:bg-gray-50"
        >

          <td class="p-4 font-medium text-gray-700">
            {{ machine.name }}
          </td>

          <td class="p-4 text-gray-500">
            <span v-if="machine.machine_models">{{ machine.machine_models.name }}</span>
            <span v-else class="text-gray-300">—</span>
          </td>

          <td class="p-4 text-gray-500">
            <span v-if="machine.embedded">{{ machine.embedded.subdomain }}</span>
            <span v-else class="text-gray-300">—</span>
          </td>

          <td class="p-4">
            <span v-if="machine.embedded" :class="statusClass(machine.embedded.status)">
              {{ machine.embedded.status }}
            </span>
          </td>

          <td class="p-4 w-40">
            <div v-if="machineHasCoils(machine)" class="flex items-center gap-2">
              <div class="flex-1 h-2 bg-gray-200 rounded-full overflow-hidden">
                <div
                  :class="machineBarClass(machine)"
                  :style="{ width: machinePct(machine) + '%' }"
                  class="h-full rounded-full transition-all"
                />
              </div>
              <span class="text-xs text-gray-500 w-8 text-right">{{ machinePct(machine) }}%</span>
            </div>
            <span v-else class="text-xs text-gray-300">—</span>
          </td>

          <td class="p-4">
            <div class="flex items-center gap-2">
              <button
                @click.stop="openInsight(machine)"
                class="flex items-center gap-1 px-2 py-1 rounded-full text-xs font-medium bg-purple-100 text-purple-700 hover:bg-purple-200 transition"
                title="AI Insight"
              >
                <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9.813 15.904L9 18.75l-.813-2.846a4.5 4.5 0 00-3.09-3.09L2.25 12l2.846-.813a4.5 4.5 0 003.09-3.09L9 5.25l.813 2.846a4.5 4.5 0 003.09 3.09L15.75 12l-2.846.813a4.5 4.5 0 00-3.09 3.09z"/>
                </svg>
                Insight
              </button>
              <button
                @click.stop="toggleMenu(machine.id, $event)"
                class="p-1.5 rounded hover:bg-gray-100 text-gray-500 hover:text-gray-700 transition"
              >
                <svg class="w-5 h-5" fill="currentColor" viewBox="0 0 20 20">
                  <circle cx="10" cy="4" r="1.5"/>
                  <circle cx="10" cy="10" r="1.5"/>
                  <circle cx="10" cy="16" r="1.5"/>
                </svg>
              </button>
            </div>
          </td>

        </tr>

        <tr v-if="!loadingMachines && machines.length === 0">
          <td colspan="6" class="p-8 text-center text-gray-400">
            No machines yet. Add your first machine.
          </td>
        </tr>

      </tbody>

    </table>

    <div v-if="loadingMachines" class="p-6 text-center text-gray-500 text-sm">
      Loading machines...
    </div>

  </div>

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
      class="w-full border rounded p-2 mb-3"
    />

    <label class="block text-sm text-gray-600 mb-1">Category</label>
    <select v-model="newMachineCategory" class="w-full border rounded p-2 mb-3">
      <option :value="null">— none —</option>
      <option value="snack">Snack</option>
      <option value="drink">Drink</option>
      <option value="frozen">Frozen</option>
      <option value="candy">Candy</option>
      <option value="personal_care">Personal Care</option>
      <option value="other">Other</option>
    </select>

    <label class="block text-sm text-gray-600 mb-1">Monthly Rent (R$)</label>
    <input
      v-model.number="newMachineRent"
      type="number"
      placeholder="0.00"
      min="0"
      step="0.01"
      class="w-full border rounded p-2 mb-4"
    />

    <p v-if="createError" class="text-red-500 text-sm mb-3">{{ createError }}</p>

    <div class="flex justify-end gap-2">

      <button
        @click="showAddModal=false"
        :disabled="createSaving"
        class="px-3 py-1 border rounded disabled:opacity-50"
      >
        Cancel
      </button>

      <button
        @click="createMachine"
        :disabled="createSaving"
        class="px-3 py-1 bg-blue-600 text-white rounded disabled:opacity-50"
      >
        {{ createSaving ? 'Creating...' : 'Create' }}
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

    <div v-if="availableEmbeddeds.length === 0" class="text-sm text-gray-400 text-center py-4 mb-4">
      No available devices. Register a new device first.
    </div>

    <select
      v-else
      v-model="selectedEmbedded"
      class="w-full border rounded p-2 mb-4"
    >
      <option :value="null" disabled>Select a device</option>
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
        :disabled="availableEmbeddeds.length === 0"
        @click="linkEmbedded"
        class="px-3 py-1 bg-green-600 text-white rounded disabled:opacity-40"
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
            <th class="p-4 w-36 text-center">Stock</th>
            <th class="p-4 w-20 text-center">Refill</th>
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

            <td class="p-4 text-center">
              <div class="flex flex-col items-center gap-1">
                <span :class="stockBadgeClass(coil)" class="text-xs font-semibold px-2 py-0.5 rounded-full">
                  {{ coil.current_stock ?? 0 }} / {{ coil.capacity ?? 0 }}
                </span>
                <div class="w-24 h-1.5 bg-gray-200 rounded-full overflow-hidden">
                  <div
                    :class="stockBarClass(coil)"
                    :style="{ width: stockPct(coil) + '%' }"
                    class="h-full rounded-full transition-all"
                  />
                </div>
              </div>
            </td>

            <td class="p-4 text-center">
              <button
                @click="refillCoil(coil)"
                :disabled="coil.current_stock === coil.capacity"
                class="px-2 py-1 text-xs bg-emerald-600 hover:bg-emerald-500 text-white rounded transition disabled:opacity-30"
              >
                Refill
              </button>
            </td>

          </tr>
        </tbody>

      </table>

    </div>

    <!-- FOOTER -->
    <div class="p-4 border-t flex justify-between items-center">
      <button
        :disabled="coils.length === 0"
        @click="refillAll"
        class="px-4 py-2 bg-emerald-600 hover:bg-emerald-500 text-white text-sm rounded transition disabled:opacity-40"
      >
        Refill All
      </button>
      <div class="flex gap-2">
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
</div>

<!-- INSIGHT MODAL -->

<div
  v-if="showInsightModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center z-50"
>
  <div class="bg-white rounded-xl w-full max-w-lg mx-4 flex flex-col max-h-[80vh]">

    <div class="p-5 border-b flex justify-between items-center">
      <div>
        <h2 class="text-lg font-semibold text-gray-800">AI Insight</h2>
        <p class="text-sm text-gray-500">{{ insightMachine?.name }}</p>
      </div>
      <button @click="closeInsight" class="text-gray-400 hover:text-gray-600 text-xl leading-none">&times;</button>
    </div>

    <div class="p-5 overflow-y-auto flex-1">
      <div v-if="insightLoading" class="flex items-center gap-3 text-gray-500 text-sm">
        <svg class="animate-spin w-4 h-4" fill="none" viewBox="0 0 24 24">
          <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"/>
          <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8v8z"/>
        </svg>
        Analyzing machine data...
      </div>
      <p v-else-if="insightError" class="text-red-500 text-sm">{{ insightError }}</p>
      <p v-else class="text-gray-700 text-sm leading-relaxed whitespace-pre-wrap">{{ insightText }}</p>
    </div>

    <div class="p-4 border-t flex justify-end">
      <button @click="closeInsight" class="px-4 py-2 border rounded text-sm text-gray-600 hover:bg-gray-50">
        Close
      </button>
    </div>

  </div>
</div>

<!-- EDIT MACHINE MODAL -->

<div
  v-if="showEditModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center z-50"
>
  <div class="bg-white p-6 rounded-xl w-80">

    <h2 class="text-lg font-semibold mb-4">Edit Machine</h2>

    <label class="block text-sm text-gray-600 mb-1">Name</label>
    <input
      v-model="editMachineName"
      placeholder="Machine name"
      class="w-full border rounded p-2 mb-3"
    />

    <label class="block text-sm text-gray-600 mb-1">Category</label>
    <select v-model="editMachineCategory" class="w-full border rounded p-2 mb-3">
      <option :value="null">— none —</option>
      <option value="snack">Snack</option>
      <option value="drink">Drink</option>
      <option value="frozen">Frozen</option>
      <option value="candy">Candy</option>
      <option value="personal_care">Personal Care</option>
      <option value="other">Other</option>
    </select>

    <label class="block text-sm text-gray-600 mb-1">Monthly Rent (R$)</label>
    <input
      v-model.number="editMachineRent"
      type="number"
      placeholder="0.00"
      min="0"
      step="0.01"
      class="w-full border rounded p-2 mb-4"
    />

    <p v-if="editError" class="text-red-500 text-sm mb-3">{{ editError }}</p>

    <div class="flex justify-end gap-2">
      <button
        @click="showEditModal = false"
        :disabled="editSaving"
        class="px-3 py-1 border rounded disabled:opacity-50"
      >
        Cancel
      </button>
      <button
        @click="saveMachineEdit"
        :disabled="editSaving"
        class="px-3 py-1 bg-yellow-500 text-white rounded disabled:opacity-50"
      >
        {{ editSaving ? 'Saving...' : 'Save' }}
      </button>
    </div>

  </div>
</div>

<!-- ACTIONS DROPDOWN (fixed to avoid overflow-hidden clipping) -->
<div
  v-if="openMenuId"
  :style="{ position: 'fixed', top: menuPosition.top + 'px', right: menuPosition.right + 'px' }"
  class="w-44 bg-white border border-gray-200 rounded-lg shadow-lg z-50 py-1"
>
  <template v-for="machine in machines" :key="machine.id">
    <template v-if="openMenuId === machine.id">
      <button
        @click.stop="openEditModal(machine); openMenuId = null"
        class="w-full text-left px-4 py-2 text-sm text-gray-700 hover:bg-gray-50"
      >Edit</button>
      <button
        @click.stop="openLinkModal(machine); openMenuId = null"
        class="w-full text-left px-4 py-2 text-sm text-gray-700 hover:bg-gray-50"
      >Link Device</button>
      <button
        @click.stop="openModelDialog(machine); openMenuId = null"
        class="w-full text-left px-4 py-2 text-sm text-gray-700 hover:bg-gray-50"
      >Link Model</button>
      <button
        @click.stop="openCoilsModal(machine); openMenuId = null"
        class="w-full text-left px-4 py-2 text-sm text-gray-700 hover:bg-gray-50"
      >Coils</button>
      <div class="border-t border-gray-100 my-1"/>
      <button
        :disabled="!machine.embedded || machine.embedded.status === 'offline'"
        @click.stop="openCreditModal(machine); openMenuId = null"
        class="w-full text-left px-4 py-2 text-sm text-green-700 hover:bg-gray-50 disabled:opacity-40 disabled:cursor-not-allowed"
      >Send Credit</button>
    </template>
  </template>
</div>

</template>

<script>

import { supabase } from "@/lib/supabase"
import { lowStockThreshold } from "@/lib/settings"

export default {

  data() {
    return {

      machines: [],
      availableEmbeddeds: [],

      showAddModal: false,
      showLinkModal: false,
      showCreditModal: false,

      newMachineName: "",
      newMachineCategory: null,
      newMachineRent: null,
      createSaving: false,
      createError: null,

      selectedMachine: null,
      selectedEmbedded: null,

      creditAmount: null,

      showModelDialog: false,
      selectedModelId: null,
      machineModels: [],

      loadingMachines: false,

      showCoilsModal: false,
      loadingCoils: false,
      savingCoils: false,
      coils: [],
      allProducts: [],

      toast: null,
      openMenuId: null,
      menuPosition: { top: 0, right: 0 },

      showInsightModal: false,
      insightMachine: null,
      insightLoading: false,
      insightText: '',
      insightError: '',
      insightAbort: null,

      showEditModal: false,
      editingMachine: null,
      editMachineName: "",
      editMachineCategory: null,
      editMachineRent: null,
      editSaving: false,
      editError: null
    }
  },

  mounted() {
    this.loadMachines()
    document.addEventListener('click', this.closeMenu)
  },

  beforeUnmount() {
    document.removeEventListener('click', this.closeMenu)
  },

  methods: {

    toggleMenu(id, event) {
      if (this.openMenuId === id) {
        this.openMenuId = null
        return
      }
      const rect = event.currentTarget.getBoundingClientRect()
      this.menuPosition = {
        top: rect.bottom + window.scrollY,
        right: window.innerWidth - rect.right
      }
      this.openMenuId = id
    },

    closeMenu() {
      this.openMenuId = null
    },

    machineHasCoils(machine) {
      return machine.machine_coils?.length > 0
    },

    machinePct(machine) {
      const coils = machine.machine_coils ?? []
      const totalStock = coils.reduce((s, c) => s + (c.current_stock ?? 0), 0)
      const totalCap   = coils.reduce((s, c) => s + (c.capacity ?? 0), 0)
      if (!totalCap) return 0
      return Math.round((totalStock / totalCap) * 100)
    },

    machineBarClass(machine) {
      const pct = this.machinePct(machine)
      const t = lowStockThreshold.value
      if (pct === 0)    return 'bg-red-500'
      if (pct <= t)     return 'bg-orange-400'
      if (pct <= t * 2) return 'bg-yellow-400'
      return 'bg-green-500'
    },

    statusClass(status) {
      return status === 'online'
        ? 'px-2 py-0.5 rounded-full text-xs font-medium bg-green-100 text-green-700'
        : 'px-2 py-0.5 rounded-full text-xs font-medium bg-red-100 text-red-700'
    },

    async loadMachines() {
      this.loadingMachines = true

      const { data, error } = await supabase
        .from("machines")
        .select(`*, machine_models(name), embedded(id, subdomain, status), machine_coils(capacity, current_stock)`)

      if (error) {
        console.error("Failed to load machines:", error)
      } else {
        this.machines = data
      }

      this.loadingMachines = false
    },

    async createMachine() {
      if (!this.newMachineName.trim()) {
        this.createError = "Machine name is required"
        return
      }

      this.createSaving = true
      this.createError = null

      try {
        const { error } = await supabase
          .from("machines")
          .insert({
            name: this.newMachineName.trim(),
            category: this.newMachineCategory ?? null,
            monthly_rent: this.newMachineRent ?? null
          })

        if (error) throw error

        this.newMachineName = ""
        this.newMachineCategory = null
        this.newMachineRent = null
        this.showAddModal = false
        await this.loadMachines()
      } catch (err) {
        this.createError = err.message ?? "Failed to create machine"
        console.error("createMachine error:", err)
      } finally {
        this.createSaving = false
      }
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
      this.showToast("Credit sent successfully")
    },

    showToast(message) {
      this.toast = message
      setTimeout(() => { this.toast = null }, 3000)
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
        .eq("enabled", true)
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
        .select("id, item_number, alias, capacity, current_stock, product_id, item_price")
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
        .select("id, name, price, current_stock")
        .eq("enabled", true)
        .order("name")

      if (!error) this.allProducts = data ?? []
    },

    onProductChange(coil) {
      const product = this.allProducts.find(p => p.id === coil.selectedProductId)
      coil.selectedItemPrice = product?.price ?? null
    },

    stockPct(coil) {
      if (!coil.capacity) return 0
      return Math.round(((coil.current_stock ?? 0) / coil.capacity) * 100)
    },

    stockBadgeClass(coil) {
      const pct = this.stockPct(coil)
      const t = lowStockThreshold.value
      if (pct === 0)    return 'bg-red-100 text-red-700'
      if (pct <= t)     return 'bg-orange-100 text-orange-700'
      if (pct <= t * 2) return 'bg-yellow-100 text-yellow-700'
      return 'bg-green-100 text-green-700'
    },

    stockBarClass(coil) {
      const pct = this.stockPct(coil)
      const t = lowStockThreshold.value
      if (pct === 0)    return 'bg-red-500'
      if (pct <= t)     return 'bg-orange-400'
      if (pct <= t * 2) return 'bg-yellow-400'
      return 'bg-green-500'
    },

    async refillCoil(coil) {
      const { error } = await supabase
        .from("machine_coils")
        .update({ current_stock: coil.capacity })
        .eq("id", coil.id)

      if (error) {
        console.error("Failed to refill coil:", error)
        return
      }

      coil.current_stock = coil.capacity
      this.showToast(`Coil ${coil.alias || coil.item_number} refilled`)
    },

    async refillAll() {
      const toRefill = this.coils.filter(c => c.current_stock !== c.capacity && c.capacity)

      const updates = toRefill.map(c =>
        supabase.from("machine_coils").update({ current_stock: c.capacity }).eq("id", c.id)
      )

      const results = await Promise.all(updates)
      const failed = results.filter(r => r.error)

      if (failed.length) {
        console.error("Some coils failed to refill:", failed)
        return
      }

      this.coils.forEach(c => { if (c.capacity) c.current_stock = c.capacity })

      await supabase
        .from("machines")
        .update({ refilled_at: new Date().toISOString() })
        .eq("id", this.selectedMachine.id)

      this.showToast("All coils refilled")
    },

    closeInsight() {
      if (this.insightAbort) {
        this.insightAbort.abort()
        this.insightAbort = null
      }
      this.showInsightModal = false
      this.insightLoading = false
    },

    async openInsight(machine) {
      if (this.insightAbort) this.insightAbort.abort()

      this.insightMachine = machine
      this.insightText = ''
      this.insightLoading = true
      this.showInsightModal = true

      const abort = new AbortController()
      this.insightAbort = abort

      try {
        const { data: cred } = await supabase
          .from('credentials')
          .select('value')
          .eq('key', 'openai_api_key')
          .maybeSingle()

        if (!cred?.value) throw new Error('OpenAI API key not configured. Add it in Settings → API Keys.')

        const since = new Date()
        since.setDate(since.getDate() - 30)

        const [salesRes, coilsRes] = await Promise.all([
          supabase
            .from('sales')
            .select('item_price, product_id')
            .eq('machine_id', machine.id)
            .gte('created_at', since.toISOString()),
          supabase
            .from('machine_coils')
            .select('alias, capacity, current_stock, item_price, products(name)')
            .eq('machine_id', machine.id)
        ])

        const sales = salesRes.data ?? []
        const coils = coilsRes.data ?? []

        const totalRevenue = sales.reduce((s, r) => s + (r.item_price ?? 0), 0)
        const totalSales = sales.length
        const ticketMedio = totalSales > 0 ? (totalRevenue / totalSales) : 0

        const totalStock = coils.reduce((s, c) => s + (c.current_stock ?? 0), 0)
        const totalCap = coils.reduce((s, c) => s + (c.capacity ?? 0), 0)
        const stockPct = totalCap > 0 ? Math.round((totalStock / totalCap) * 100) : 0

        const productCount = {}
        for (const s of sales) {
          if (s.product_id) productCount[s.product_id] = (productCount[s.product_id] ?? 0) + 1
        }
        const topProductIds = Object.entries(productCount)
          .sort((a, b) => b[1] - a[1])
          .slice(0, 3)
          .map(([id]) => id)
        const topProducts = coils
          .filter(c => c.products && topProductIds.includes(c.products?.id))
          .map(c => c.products?.name)
          .filter(Boolean)

        const lang = navigator.language || 'en'
        const prompt = `You are a vending machine business analyst. Analyze the following data and provide practical insights and recommendations. Respond in the user's language: ${lang}.

Machine: ${machine.name}
Category: ${machine.category ?? 'not defined'}
Monthly Rent: R$ ${machine.monthly_rent?.toFixed(2) ?? 'not defined'}
Status: ${machine.embedded?.status ?? 'unknown'}

Last 30 days:
- Total sales: ${totalSales}
- Total revenue: R$ ${totalRevenue.toFixed(2)}
- Average ticket: R$ ${ticketMedio.toFixed(2)}
- Stock occupancy: ${stockPct}%
- Top selling products: ${topProducts.length ? topProducts.join(', ') : 'no data'}

Provide 3 to 5 concise and actionable insights about this machine's performance, profitability, and opportunities for improvement.`

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
        this.insightText = json.choices[0].message.content

      } catch (err) {
        if (err.name === 'AbortError') return
        this.insightError = err.message
        console.error('openInsight error:', err)
      } finally {
        this.insightAbort = null
        this.insightLoading = false
      }
    },

    openEditModal(machine) {
      this.editingMachine = machine
      this.editMachineName = machine.name ?? ""
      this.editMachineCategory = machine.category ?? null
      this.editMachineRent = machine.monthly_rent ?? null
      this.editError = null
      this.showEditModal = true
    },

    async saveMachineEdit() {
      if (!this.editMachineName.trim()) return

      this.editSaving = true
      this.editError = null

      try {
        const { error } = await supabase
          .from("machines")
          .update({
            name: this.editMachineName.trim(),
            category: this.editMachineCategory ?? null,
            monthly_rent: this.editMachineRent ?? null
          })
          .eq("id", this.editingMachine.id)

        if (error) throw error

        this.showEditModal = false
        this.editingMachine = null
        await this.loadMachines()
        this.showToast("Machine updated")
      } catch (err) {
        this.editError = err.message ?? "Failed to save"
        console.error("saveMachineEdit error:", err)
      } finally {
        this.editSaving = false
      }
    },

    categoryClass(cat) {
      const map = {
        snack:         'bg-yellow-100 text-yellow-700',
        drink:         'bg-blue-100 text-blue-700',
        frozen:        'bg-cyan-100 text-cyan-700',
        candy:         'bg-pink-100 text-pink-700',
        personal_care: 'bg-purple-100 text-purple-700',
        other:         'bg-slate-100 text-slate-600',
      }
      return map[cat] ?? 'bg-slate-100 text-slate-600'
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

<style scoped>
.fade-enter-active, .fade-leave-active { transition: opacity 0.3s, transform 0.3s; }
.fade-enter-from, .fade-leave-to { opacity: 0; transform: translateY(8px); }
</style>
