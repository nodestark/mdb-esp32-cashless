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

    <div>
      <label class="block text-sm text-gray-600 mb-1">
        Warehouse low stock threshold (units)
      </label>
      <div class="flex items-center gap-3">
        <input
          v-model.number="warehouseThreshold"
          type="number"
          min="0"
          step="1"
          class="w-24 border rounded p-2 text-sm"
        />
      </div>
      <p class="text-xs text-gray-400 mt-1">
        Products in the warehouse (not in a machine) below this quantity are flagged as "Low" in Inventory.
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

  <!-- API KEYS -->
  <div class="bg-white rounded-xl shadow p-6 space-y-4">

    <h2 class="font-semibold text-gray-700">API Keys</h2>

    <div class="space-y-4">
      <div>
        <label class="block text-sm text-gray-600 mb-1">OpenAI API Key</label>
        <input
          v-model="claudeApiKey"
          type="password"
          placeholder="sk-..."
          class="w-full border rounded p-2 text-sm font-mono"
        />
        <p class="text-xs text-gray-400 mt-1">
          Used to generate AI insights per machine.
        </p>
      </div>

      <hr class="border-gray-100" />

      <div>
        <div class="flex items-center gap-2 mb-1">
          <label class="block text-sm text-gray-600 font-medium">Stripe Secret Key</label>
          <div class="group relative">
            <span class="cursor-help text-slate-400 bg-slate-100 rounded-full w-4 h-4 flex items-center justify-center text-[10px] font-bold">?</span>
            <div class="invisible group-hover:visible absolute left-full ml-2 top-0 w-64 p-3 bg-slate-800 text-white text-xs rounded-lg shadow-xl z-50 leading-relaxed">
              Found in Stripe Dashboard > Developers > API keys. 
              Always use the <b>Secret Key (sk_...)</b>. 
              This allows the system to create payment sessions on your behalf.
            </div>
          </div>
        </div>
        <input
          v-model="stripeSecretKey"
          type="password"
          placeholder="sk_live_..."
          class="w-full border rounded p-2 text-sm font-mono"
        />
      </div>

      <div>
        <div class="flex items-center gap-2 mb-1">
          <label class="block text-sm text-gray-600 font-medium">Stripe Webhook Secret</label>
          <div class="group relative">
            <span class="cursor-help text-slate-400 bg-slate-100 rounded-full w-4 h-4 flex items-center justify-center text-[10px] font-bold">?</span>
            <div class="invisible group-hover:visible absolute left-full ml-2 top-0 w-64 p-3 bg-slate-800 text-white text-xs rounded-lg shadow-xl z-50 leading-relaxed">
              Found in Stripe Dashboard > Developers > Webhooks.
              Add an endpoint pointing to: <code>https://supabase.vmflow.xyz/functions/v1/webhook-stripe</code>.
              Listen for the <b>checkout.session.completed</b> event.
            </div>
          </div>
        </div>
        <input
          v-model="stripeWebhookSecret"
          type="password"
          placeholder="whsec_..."
          class="w-full border rounded p-2 text-sm font-mono"
        />
      </div>

      <hr class="border-gray-100" />

      <div>
        <div class="flex items-center gap-2 mb-1">
          <label class="block text-sm text-gray-600 font-medium">Mercado Pago Access Token</label>
          <div class="group relative">
            <span class="cursor-help text-slate-400 bg-slate-100 rounded-full w-4 h-4 flex items-center justify-center text-[10px] font-bold">?</span>
            <div class="invisible group-hover:visible absolute left-full ml-2 top-0 w-64 p-3 bg-slate-800 text-white text-xs rounded-lg shadow-xl z-50 leading-relaxed">
              Found in Mercado Pago Developers > Your credentials > Production credentials.
              Use the <b>Access Token (APP_USR-...)</b>.
              Configure the webhook at: <code>https://supabase.vmflow.xyz/functions/v1/webhook-mp</code>.
              Topic: <b>payment</b>.
            </div>
          </div>
        </div>
        <input
          v-model="mpAccessToken"
          type="password"
          placeholder="APP_USR-..."
          class="w-full border rounded p-2 text-sm font-mono"
        />
      </div>
    </div>

    <p v-if="apiKeyError" class="text-sm text-red-500">{{ apiKeyError }}</p>
    <p v-if="apiKeySuccess" class="text-sm text-green-600">{{ apiKeySuccess }}</p>

    <div class="flex justify-end">
      <button
        @click="saveApiKeys"
        :disabled="savingApiKey"
        class="px-4 py-2 bg-slate-800 hover:bg-slate-700 text-white text-sm rounded transition disabled:opacity-40"
      >
        {{ savingApiKey ? 'Saving...' : 'Save' }}
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
import { lowStockThreshold, warehouseLowStockThreshold } from '@/lib/settings'

const threshold = ref(lowStockThreshold.value)
const warehouseThreshold = ref(warehouseLowStockThreshold.value)

function saveThreshold() {
  lowStockThreshold.value = threshold.value
  warehouseLowStockThreshold.value = warehouseThreshold.value
}

const claudeApiKey = ref('')
const stripeSecretKey = ref('')
const stripeWebhookSecret = ref('')
const mpAccessToken = ref('')
const savingApiKey = ref(false)
const apiKeyError = ref('')
const apiKeySuccess = ref('')

async function loadApiKeys() {
  const { data } = await supabase
    .from('credentials')
    .select('key, value')
    .in('key', ['openai_api_key', 'stripe_secret_key', 'stripe_webhook_secret', 'mp_access_token'])

  if (data) {
    data.forEach(item => {
      if (item.key === 'openai_api_key') claudeApiKey.value = item.value
      if (item.key === 'stripe_secret_key') stripeSecretKey.value = item.value
      if (item.key === 'stripe_webhook_secret') stripeWebhookSecret.value = item.value
      if (item.key === 'mp_access_token') mpAccessToken.value = item.value
    })
  }
}

async function saveApiKeys() {
  apiKeyError.value = ''
  apiKeySuccess.value = ''

  savingApiKey.value = true

  const keysToSave = [
    { key: 'openai_api_key', value: claudeApiKey.value.trim() },
    { key: 'stripe_secret_key', value: stripeSecretKey.value.trim() },
    { key: 'stripe_webhook_secret', value: stripeWebhookSecret.value.trim() },
    { key: 'mp_access_token', value: mpAccessToken.value.trim() }
  ].filter(k => k.value)

  const { error } = await supabase
    .from('credentials')
    .upsert(keysToSave, { onConflict: 'owner_id,key' })

  savingApiKey.value = false

  if (error) {
    apiKeyError.value = error.message
  } else {
    apiKeySuccess.value = 'API keys saved.'
  }
}

loadApiKeys()

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
