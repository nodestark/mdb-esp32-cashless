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

    <!-- LOGIN AREA -->
    <div class="flex flex-1 items-center justify-center bg-gray-100 px-6">

      <div class="w-full max-w-md bg-white p-10 rounded-2xl shadow-lg">

        <div class="text-center mb-8">
          <h2 class="text-2xl font-bold text-gray-800">
            Sign in
          </h2>
          <p class="text-gray-500 text-sm mt-1">
            Access your account
          </p>
        </div>

        <form @submit.prevent="login" class="space-y-4">

          <input
            v-model="email"
            type="email"
            placeholder="Email"
            required
            class="w-full px-4 py-3 border border-gray-300 rounded-lg
            focus:outline-none focus:ring-2 focus:ring-slate-800"
          />

          <input
            v-model="password"
            type="password"
            placeholder="Password"
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
            {{ loading ? 'Signing in...' : 'Sign in' }}
          </button>

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
              or
            </span>
          </div>

        </div>

        <!-- REGISTER -->
        <RouterLink
          to="/register"
          class="block w-full text-center py-3 border border-slate-900
          text-slate-900 font-medium rounded-lg hover:bg-slate-900
          hover:text-white transition"
        >
          Create an account
        </RouterLink>

      </div>

    </div>

  </div>
</template>

<script>
import { supabase } from '@/lib/supabase'
import { useRouter } from 'vue-router'
import { ref } from 'vue'

export default {
  setup() {

    const router = useRouter()

    const email = ref('')
    const password = ref('')
    const loading = ref(false)
    const error = ref(null)

    const login = async () => {

      loading.value = true
      error.value = null

      const { error: loginError } = await supabase.auth.signInWithPassword({
        email: email.value,
        password: password.value
      })

      loading.value = false

      if (loginError) {
        error.value = loginError.message
        return
      }

      router.push('/dashboard')
    }

    return {
      email,
      password,
      login,
      loading,
      error
    }

  }
}
</script>