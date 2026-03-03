<template>
  <div class="min-h-screen flex items-center justify-center bg-gray-100 px-4">

    <div class="w-full max-w-md bg-white p-8 rounded-2xl shadow-lg">

      <h2 class="text-2xl font-bold text-center mb-6">
        Login
      </h2>

      <form @submit.prevent="login" class="space-y-4">

        <input
          v-model="email"
          type="email"
          placeholder="Email"
          required
          class="w-full px-4 py-3 border border-gray-300 rounded-lg focus:outline-none focus:ring-2 focus:ring-blue-500"
        />

        <input
          v-model="password"
          type="password"
          placeholder="Senha"
          required
          class="w-full px-4 py-3 border border-gray-300 rounded-lg focus:outline-none focus:ring-2 focus:ring-blue-500"
        />

        <button
          type="submit"
          :disabled="loading"
          class="w-full py-3 bg-blue-600 text-white font-medium rounded-lg hover:bg-blue-700 transition disabled:opacity-60 disabled:cursor-not-allowed"
        >
          {{ loading ? 'Entrando...' : 'Entrar' }}
        </button>

        <p
          v-if="error"
          class="text-red-500 text-sm mt-2 text-center"
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
            ou
          </span>
        </div>
      </div>

      <!-- Register Button -->
      <RouterLink
        to="/register"
        class="block w-full text-center py-3 bg-green-600 text-white font-medium rounded-lg hover:bg-green-700 transition"
      >
        Criar uma conta
      </RouterLink>

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

    return { email, password, login, loading, error }
  }
}
</script>