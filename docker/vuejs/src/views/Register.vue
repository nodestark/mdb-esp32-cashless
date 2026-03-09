<template>
  <div class="min-h-screen flex">

    <!-- LEFT BRAND -->
    <div class="hidden lg:flex lg:w-1/2 bg-slate-900 text-white flex-col justify-center px-20">

      <h1 class="text-4xl font-bold mb-4">
        VMFlow
      </h1>

      <p class="text-slate-400 text-lg max-w-md">
        Manage vending machines, monitor sales, devices,
        and telemetry from a single platform.
      </p>

    </div>

    <!-- REGISTER AREA -->
    <div class="flex flex-1 items-center justify-center bg-gray-100 px-6">

      <div class="w-full max-w-md bg-white p-10 rounded-2xl shadow-lg">

        <div class="text-center mb-8">
          <h2 class="text-2xl font-bold text-gray-800">
            Create an account
          </h2>
          <p class="text-gray-500 text-sm mt-1">
            Start managing your vending machines
          </p>
        </div>

        <form @submit.prevent="register" class="space-y-4">

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
            {{ loading ? 'Creating account...' : 'Create account' }}
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

        <!-- Divider -->
        <div class="relative my-8">

          <div class="absolute inset-0 flex items-center">
            <div class="w-full border-t border-gray-300"></div>
          </div>

          <div class="relative text-center">
            <span class="bg-white px-3 text-sm text-gray-500">
              already have an account?
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
import { useRouter } from 'vue-router'

export default {
  name: 'Register',

  setup() {

    const router = useRouter()

    const email = ref('')
    const password = ref('')
    const loading = ref(false)
    const error = ref(null)
    const message = ref(null)

    const register = async () => {

      loading.value = true
      error.value = null
      message.value = null

      const { error: registerError } = await supabase.auth.signUp({
        email: email.value,
        password: password.value
      })

      loading.value = false

      if (registerError) {
        error.value = registerError.message
        return
      }

      message.value = "Account created! Check your email to confirm."

      setTimeout(() => {
        router.push('/login')
      }, 2000)

    }

    return {
      email,
      password,
      register,
      loading,
      error,
      message
    }

  }
}
</script>