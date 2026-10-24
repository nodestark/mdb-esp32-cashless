<template>
  <div class="min-h-screen bg-gray-100 flex items-center justify-center p-4">
    <div class="max-w-md w-full bg-white rounded-2xl shadow-xl overflow-hidden">
      <div class="bg-slate-800 p-6 text-center">
        <h1 class="text-white text-2xl font-bold">{{ machineName || 'Vending Machine' }}</h1>
        <p class="text-slate-300 text-sm mt-1">Ready to receive credit</p>
      </div>

      <div class="p-8 space-y-6">
        <div v-if="loadingMachine" class="text-center py-10">
          <div class="animate-spin rounded-full h-10 w-10 border-b-2 border-slate-800 mx-auto"></div>
          <p class="mt-4 text-gray-500 font-medium">Loading machine details...</p>
        </div>

        <div v-else-if="machineError" class="text-center py-10">
          <div class="text-red-500 text-5xl mb-4">⚠️</div>
          <p class="text-gray-700 font-medium">{{ machineError }}</p>
          <button @click="reload" class="mt-4 text-slate-600 underline">Try again</button>
        </div>

        <div v-else-if="paymentSuccess" class="text-center py-10">
          <div class="text-green-500 text-6xl mb-4">✓</div>
          <p class="text-gray-800 text-lg font-bold">Payment successful!</p>
          <p class="text-gray-500 text-sm mt-2">Credit is being sent to the machine.</p>
          <button @click="resetSuccess" class="mt-6 w-full bg-slate-800 hover:bg-slate-700 text-white font-bold py-3 rounded-xl transition-all">
            Add more credit
          </button>
        </div>

        <div v-else-if="paymentCanceled" class="text-center py-10">
          <div class="text-yellow-500 text-5xl mb-4">✕</div>
          <p class="text-gray-700 font-medium">Payment canceled.</p>
          <button @click="resetCanceled" class="mt-4 text-slate-600 underline">Try again</button>
        </div>

        <div v-else class="space-y-6">
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-2">
              Enter amount to pay
            </label>
            <div class="relative">
              <span class="absolute left-4 top-1/2 -translate-y-1/2 text-gray-400 font-bold text-xl">{{ currencySymbolOf(machineCurrency) }}</span>
              <input
                v-model="amount"
                type="number"
                step="0.01"
                min="0.50"
                class="w-full pl-14 pr-4 py-4 border-2 border-gray-200 rounded-xl text-2xl font-bold focus:border-slate-800 focus:ring-0 transition-colors"
                placeholder="0.00"
              />
            </div>
            <p class="text-xs text-gray-400 mt-2">Minimum amount: {{ formatMoney(0.5, machineCurrency) }}</p>
          </div>

          <button
            @click="handlePayment"
            :disabled="paying || !amount || amount < 0.5"
            class="w-full bg-slate-800 hover:bg-slate-700 text-white font-bold py-4 rounded-xl shadow-lg transform active:scale-[0.98] transition-all disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center"
          >
            <span v-if="paying">
              <svg class="animate-spin -ml-1 mr-3 h-5 w-5 text-white" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
                <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
                <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
              </svg>
              Processing...
            </span>
            <span v-else>Pay with Card</span>
          </button>

          <div class="flex items-center justify-center gap-4 pt-4 border-t border-gray-100">
            <img src="https://upload.wikimedia.org/wikipedia/commons/b/ba/Stripe_Logo%2C_revised_2016.svg" alt="Stripe" class="h-6 opacity-50" />
          </div>
        </div>
      </div>

      <div class="bg-gray-50 px-8 py-4 text-center">
        <p class="text-xs text-gray-400">
          Powered by <strong>VMFlow</strong>. Your payment is secure and encrypted.
        </p>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { supabase } from '@/lib/supabase'
import { formatMoney, currencySymbolOf } from '@/lib/currency'

const route = useRoute()
const router = useRouter()
const machineId = route.params.machineId

const amount = ref('')
const machineName = ref('')
const machineCurrency = ref('USD')
const loadingMachine = ref(true)
const machineError = ref('')
const paying = ref(false)
const paymentSuccess = ref(false)
const paymentCanceled = ref(false)

function reload() {
  window.location.reload()
}

function resetSuccess() {
  router.replace({ query: {} })
  paymentSuccess.value = false
}

function resetCanceled() {
  router.replace({ query: {} })
  paymentCanceled.value = false
}

async function fetchMachine() {
  try {
    const { data, error } = await supabase
      .rpc('get_machine_name', { machine_id: machineId })

    if (error) throw error
    if (data) machineName.value = data
    else machineError.value = 'Machine not found or unavailable.'

    const { data: code } = await supabase
      .rpc('get_machine_currency', { machine_id: machineId })
    if (code) machineCurrency.value = code
  } catch (err) {
    console.error('Error fetching machine:', err)
    machineError.value = 'Machine not found or unavailable.'
  } finally {
    loadingMachine.value = false
  }
}

async function handlePayment() {
  if (paying.value) return
  paying.value = true

  try {
    const { data, error } = await supabase.functions.invoke('create-checkout-stripe', {
      body: {
        machineId,
        amount: parseFloat(amount.value)
      }
    })

    if (error) throw error

    if (data?.url) {
      window.location.href = data.url
    } else {
      throw new Error('Failed to create payment session.')
    }
  } catch (err) {
    let message = err.message || 'Please try again.'
    if (err.context) {
      try {
        const body = await err.context.json()
        message = body?.error || body?.msg || message
      } catch {}
    }
    alert('Payment error: ' + message)
    paying.value = false
  }
}

onMounted(() => {
  if (route.query.success === 'true') {
    paymentSuccess.value = true
  } else if (route.query.canceled === 'true') {
    paymentCanceled.value = true
  }
  fetchMachine()
})
</script>
