<template>

<div class="p-6">

  <!-- HEADER -->

  <div class="flex justify-between items-center mb-6">
    <h1 class="text-2xl font-bold">Machine Models</h1>

    <button
      @click="openCreate"
      class="px-4 py-2 bg-slate-800 text-white rounded"
    >
      + Add Model
    </button>
  </div>


  <!-- TABLE -->

  <table class="w-full border rounded-xl overflow-hidden">

    <thead class="bg-gray-100 text-left">
      <tr>
        <th class="p-3">Name</th>
        <th class="p-3">Manufacturer</th>
        <th class="p-3">Coils</th>
        <th class="p-3"></th>
      </tr>
    </thead>

    <tbody>

      <tr
        v-for="model in models"
        :key="model.id"
        class="border-t"
      >
        <td class="p-3">
          {{ model.name }}
        </td>

        <td class="p-3">
          {{ model.manufacturer }}
        </td>

        <td class="p-3">
          {{ model.coils_count }}
        </td>

        <td class="p-3">
          <button
            @click="confirmDelete(model)"
            class="px-3 py-1 text-sm border border-red-300 hover:bg-red-50 text-red-600 rounded transition"
          >
            Delete
          </button>
        </td>

      </tr>

    </tbody>

  </table>

</div>


<!-- CREATE MODAL -->

<div
  v-if="showModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>

  <div class="bg-white p-6 rounded-xl w-[520px]">

    <h2 class="text-lg font-semibold mb-4">
      Create Machine Model
    </h2>


    <!-- MODEL DATA -->

    <input
      v-model="modelName"
      placeholder="Model name"
      class="w-full border rounded p-2 mb-3"
    />

    <input
      v-model="manufacturer"
      placeholder="Manufacturer"
      class="w-full border rounded p-2 mb-4"
    />


    <!-- COILS -->

    <div class="mb-3 flex justify-between items-center">
      <h3 class="font-medium">
        Coils
      </h3>

      <button
        @click="addCoil"
        class="text-sm px-3 py-1 bg-slate-800 text-white rounded"
      >
        + Add Coil
      </button>
    </div>


    <div class="space-y-2 max-h-60 overflow-y-auto">

      <div
        v-for="(coil, index) in coils"
        :key="index"
        class="flex gap-2 items-center"
      >

        <input
          :value="coil.alias"
          @input="coil.alias = $event.target.value.toUpperCase()"
          placeholder="Alias (A1, B3...)"
          class="border rounded p-2 w-28"
          :class="{ 'border-red-400': coilErrors[index]?.alias }"
        />

        <input
          v-model.number="coil.capacity"
          type="number"
          placeholder="Capacity"
          min="1"
          class="border rounded p-2 w-28"
          :class="{ 'border-red-400': coilErrors[index]?.capacity }"
        />

        <button
          @click="removeCoil(index)"
          class="px-2 py-1 text-sm bg-red-600 text-white rounded"
        >
          X
        </button>

      </div>

    </div>

    <!-- VALIDATION ERROR -->

    <p v-if="formError" class="text-red-500 text-sm mt-3">
      {{ formError }}
    </p>


    <!-- ACTIONS -->

    <div class="flex justify-end gap-2 mt-6">

      <button
        @click="closeModal"
        class="px-3 py-1 border rounded"
      >
        Cancel
      </button>

      <button
        @click="createModel"
        :disabled="saving"
        class="px-3 py-1 bg-blue-600 text-white rounded disabled:opacity-50"
      >
        {{ saving ? 'Saving...' : 'Create' }}
      </button>

    </div>

  </div>

</div>

<!-- DELETE CONFIRM MODAL -->

<div
  v-if="showDeleteModal"
  class="fixed inset-0 bg-black/40 flex items-center justify-center"
>
  <div class="bg-white p-6 rounded-xl w-96">

    <h2 class="text-lg font-semibold mb-2">Disable Model</h2>

    <p class="text-gray-600 mb-6">
      Are you sure you want to disable
      <span class="font-medium text-gray-900">{{ modelToDelete?.name }}</span>?
    </p>

    <div class="flex justify-end gap-2">
      <button
        @click="showDeleteModal = false"
        class="px-3 py-1 border rounded text-sm"
      >
        Cancel
      </button>
      <button
        @click="deleteModel"
        :disabled="deleting"
        class="px-3 py-1 bg-red-600 hover:bg-red-500 text-white text-sm rounded transition disabled:opacity-50"
      >
        {{ deleting ? 'Deleting...' : 'Delete' }}
      </button>
    </div>

  </div>
</div>

</template>



<script setup>

import { ref, onMounted } from "vue"
import { supabase } from "@/lib/supabase"

const models = ref([])

const showModal = ref(false)
const saving = ref(false)
const formError = ref("")
const coilErrors = ref([])

const showDeleteModal = ref(false)
const modelToDelete = ref(null)
const deleting = ref(false)

const modelName = ref("")
const manufacturer = ref("")
const coils = ref([])

function openCreate(){
  modelName.value = ""
  manufacturer.value = ""
  coils.value = []
  formError.value = ""
  coilErrors.value = []
  showModal.value = true
}

function closeModal(){
  modelName.value = ""
  manufacturer.value = ""
  coils.value = []
  formError.value = ""
  coilErrors.value = []
  showModal.value = false
}

function addCoil(){
  coils.value.push({ alias: "", capacity: null })
  coilErrors.value.push({})
}

function removeCoil(index){
  coils.value.splice(index, 1)
  coilErrors.value.splice(index, 1)
}

function validate(){
  formError.value = ""
  coilErrors.value = coils.value.map(() => ({}))

  if(!modelName.value.trim()){
    formError.value = "Model name is required."
    return false
  }

  if(!manufacturer.value.trim()){
    formError.value = "Manufacturer is required."
    return false
  }

  let hasCoilError = false

  coils.value.forEach((coil, i) => {
    if(!coil.alias.trim()){
      coilErrors.value[i].alias = true
      hasCoilError = true
    }
    if(!coil.capacity || coil.capacity <= 0){
      coilErrors.value[i].capacity = true
      hasCoilError = true
    }
  })

  if(hasCoilError){
    formError.value = "Fill in all coil fields with valid values."
    return false
  }

  return true
}

function confirmDelete(model){
  modelToDelete.value = model
  showDeleteModal.value = true
}

async function deleteModel(){
  if(!modelToDelete.value) return

  deleting.value = true

  try {
    const { error } = await supabase
      .from("machine_models")
      .update({ enabled: false })
      .eq("id", modelToDelete.value.id)

    if(error) throw error

    showDeleteModal.value = false
    modelToDelete.value = null
    await loadModels()

  } catch(err) {
    console.error("Failed to disable model:", err)
  } finally {
    deleting.value = false
  }
}

async function loadModels(){

  const { data, error } = await supabase
    .from("machine_models")
    .select(`
      id,
      name,
      manufacturer,
      model_coils(id)
    `)
    .eq("enabled", true)

  if(error){
    console.error("Failed to load models:", error)
    return
  }

  models.value = data.map(m => ({
    ...m,
    coils_count: m.model_coils?.length || 0
  }))

}

async function createModel(){

  if(!validate()) return

  saving.value = true

  try {

    const { data: model, error } = await supabase
      .from("machine_models")
      .insert({
        name: modelName.value.trim(),
        manufacturer: manufacturer.value.trim()
      })
      .select()
      .single()

    if(error){
      formError.value = "Error creating model. Try again."
      console.error(error)
      return
    }

    if(coils.value.length){

      const rows = coils.value.map(c => ({
        model_id: model.id,
        alias: c.alias.trim(),
        capacity: c.capacity
      }))

      const { error: coilError } = await supabase
        .from("model_coils")
        .insert(rows)

      if(coilError){
        formError.value = "Model created but failed to save coils."
        console.error(coilError)
        return
      }

    }

    closeModal()
    await loadModels()

  } finally {
    saving.value = false
  }

}

onMounted(loadModels)

</script>