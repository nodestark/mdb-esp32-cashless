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

    <!-- FORGOT PASSWORD AREA -->
    <div class="flex flex-1 items-center justify-center bg-gray-100 px-6">

      <div class="w-full max-w-md bg-white p-10 rounded-2xl shadow-lg">

        <div class="text-center mb-8">
          <h2 class="text-2xl font-bold text-gray-800">
            Forgot password
          </h2>
          <p class="text-gray-500 text-sm mt-1">
            Enter your email and we'll send you a reset link
          </p>
        </div>

        <form @submit.prevent="sendReset" class="space-y-4">

          <input
            v-model="email"
            type="email"
            placeholder="Email"
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
            {{ loading ? 'Sending...' : 'Send reset link' }}
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

        <!-- divider -->
        <div class="relative my-8">

          <div class="absolute inset-0 flex items-center">
            <div class="w-full border-t border-gray-300"></div>
          </div>

          <div class="relative text-center">
            <span class="bg-white px-3 text-sm text-gray-500">
              remembered your password?
            </span>
          </div>

        </div>

        <RouterLink
          to="/login"
          class="block w-full text-center py-3 border border-slate-900
          text-slate-900 font-medium rounded-lg hover:bg-slate-900
          hover:text-white transition"
        >
          Sign in
        </RouterLink>

      </div>

    </div>

  </div>
</template>

<script>
import { ref } from 'vue'
import { supabase } from '@/lib/supabase'

export default {
  setup() {

    const email = ref('')
    const loading = ref(false)
    const error = ref(null)
    const message = ref(null)

    const sendReset = async () => {

      loading.value = true
      error.value = null
      message.value = null

      const { error: resetError } = await supabase.auth.resetPasswordForEmail(
        email.value,
        { redirectTo: `${window.location.origin}/reset-password` }
      )

      loading.value = false

      if (resetError) {
        error.value = resetError.message
        return
      }

      message.value = 'Check your email for the password reset link.'

    }

    return {
      email,
      sendReset,
      loading,
      error,
      message
    }

  }
}
</script>
