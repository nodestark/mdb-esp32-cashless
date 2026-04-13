<template>
  <div class="min-h-screen flex">

    <!-- LEFT BRAND -->
    <div class="hidden lg:flex lg:w-1/2 bg-slate-900 text-white flex-col justify-center px-20">

      <h1 class="text-4xl font-bold mb-4">
        VMFlow
      </h1>

      <p class="text-slate-400 text-lg max-w-md">
        A platform for managing vending machines. Monitor sales,
        devices, and telemetry in real time.
      </p>

    </div>

    <!-- RESET PASSWORD AREA -->
    <div class="flex flex-1 items-center justify-center bg-gray-100 px-6">

      <div class="w-full max-w-md bg-white p-10 rounded-2xl shadow-lg">

        <div class="text-center mb-8">
          <h2 class="text-2xl font-bold text-gray-800">
            Reset password
          </h2>
          <p class="text-gray-500 text-sm mt-1">
            Enter your new password
          </p>
        </div>

        <form @submit.prevent="updatePassword" class="space-y-4">

          <input
            v-model="password"
            type="password"
            placeholder="New password"
            required
            class="w-full px-4 py-3 border border-gray-300 rounded-lg
            focus:outline-none focus:ring-2 focus:ring-slate-800"
          />

          <input
            v-model="confirmPassword"
            type="password"
            placeholder="Confirm new password"
            required
            class="w-full px-4 py-3 border border-gray-300 rounded-lg
            focus:outline-none focus:ring-2 focus:ring-slate-800"
          />

          <button
            type="submit"
            :disabled="loading"
            class="w-full py-3 bg-slate-900 text-white font-medium rounded-lg
            hover:bg-slate-800 transition disabled:opacity-60"
          >
            {{ loading ? 'Updating...' : 'Update password' }}
          </button>

          <p
            v-if="message"
            class="text-green-600 text-sm text-center"
          >
            {{ message }}
          </p>

          <p
            v-if="error"
            class="text-red-500 text-sm text-center"
          >
            {{ error }}
          </p>

        </form>

      </div>

    </div>

  </div>
</template>

<script>
import { ref } from 'vue'
import { supabase } from '@/lib/supabase'
import { useRouter } from 'vue-router'

export default {
  setup() {

    const router = useRouter()

    const password = ref('')
    const confirmPassword = ref('')
    const loading = ref(false)
    const error = ref(null)
    const message = ref(null)

    const updatePassword = async () => {

      loading.value = true
      error.value = null
      message.value = null

      if (password.value !== confirmPassword.value) {
        error.value = 'Passwords do not match.'
        loading.value = false
        return
      }

      const { error: updateError } = await supabase.auth.updateUser({
        password: password.value
      })

      loading.value = false

      if (updateError) {
        error.value = updateError.message
        return
      }

      message.value = 'Password updated successfully!'

      setTimeout(() => {
        router.push('/dashboard')
      }, 2000)

    }

    return {
      password,
      confirmPassword,
      updatePassword,
      loading,
      error,
      message
    }

  }
}
</script>
