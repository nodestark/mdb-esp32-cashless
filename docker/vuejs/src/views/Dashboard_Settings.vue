<template>

<div class="p-6 space-y-6 max-w-xl">

  <div>
    <h1 class="text-2xl font-bold text-gray-800">Settings</h1>
    <p class="text-gray-500">Application preferences and account</p>
  </div>

  <!-- INVENTORY -->
  <div class="bg-white rounded-xl shadow p-6 space-y-4">

    <h2 class="font-semibold text-gray-700">Inventory</h2>

    <div>
      <label class="block text-sm text-gray-600 mb-1">
        Low stock threshold (%)
      </label>
      <div class="flex items-center gap-3">
        <input
          v-model.number="threshold"
          type="range"
          min="5"
          max="50"
          step="5"
          class="flex-1 accent-slate-700"
        />
        <span class="w-12 text-center text-sm font-semibold text-gray-700 border rounded px-2 py-1">
          {{ threshold }}%
        </span>
      </div>
      <p class="text-xs text-gray-400 mt-1">
        Coils and products below this fill level are flagged as "Low".
      </p>
    </div>

    <div class="flex justify-end">
      <button
        @click="saveThreshold"
        class="px-4 py-2 bg-slate-800 hover:bg-slate-700 text-white text-sm rounded transition"
      >
        Save
      </button>
    </div>

  </div>

  <!-- CHANGE PASSWORD -->
  <div class="bg-white rounded-xl shadow p-6 space-y-4">

    <h2 class="font-semibold text-gray-700">Change Password</h2>

    <div>
      <label class="block text-sm text-gray-600 mb-1">New password</label>
      <input
        v-model="newPassword"
        type="password"
        placeholder="Min. 6 characters"
        class="w-full border rounded p-2 text-sm"
      />
    </div>

    <div>
      <label class="block text-sm text-gray-600 mb-1">Confirm password</label>
      <input
        v-model="confirmPassword"
        type="password"
        placeholder="Repeat new password"
        class="w-full border rounded p-2 text-sm"
      />
    </div>

    <p v-if="passwordError" class="text-sm text-red-500">{{ passwordError }}</p>
    <p v-if="passwordSuccess" class="text-sm text-green-600">{{ passwordSuccess }}</p>

    <div class="flex justify-end">
      <button
        @click="changePassword"
        :disabled="savingPassword"
        class="px-4 py-2 bg-slate-800 hover:bg-slate-700 text-white text-sm rounded transition disabled:opacity-40"
      >
        {{ savingPassword ? 'Saving...' : 'Update Password' }}
      </button>
    </div>

  </div>

</div>

</template>

<script setup>
import { ref } from 'vue'
import { supabase } from '@/lib/supabase'
import { lowStockThreshold } from '@/lib/settings'

const threshold = ref(lowStockThreshold.value)

function saveThreshold() {
  lowStockThreshold.value = threshold.value
}

const newPassword = ref('')
const confirmPassword = ref('')
const savingPassword = ref(false)
const passwordError = ref('')
const passwordSuccess = ref('')

async function changePassword() {
  passwordError.value = ''
  passwordSuccess.value = ''

  if (newPassword.value.length < 6) {
    passwordError.value = 'Password must be at least 6 characters.'
    return
  }
  if (newPassword.value !== confirmPassword.value) {
    passwordError.value = 'Passwords do not match.'
    return
  }

  savingPassword.value = true
  const { error } = await supabase.auth.updateUser({ password: newPassword.value })
  savingPassword.value = false

  if (error) {
    passwordError.value = error.message
  } else {
    passwordSuccess.value = 'Password updated successfully.'
    newPassword.value = ''
    confirmPassword.value = ''
  }
}
</script>
