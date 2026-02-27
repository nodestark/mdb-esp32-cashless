<template>
  <div class="min-h-screen flex items-center justify-center bg-gray-100 px-4">

    <div class="w-full max-w-md bg-white p-8 rounded-2xl shadow-lg">

      <h2 class="text-2xl font-bold text-center mb-6">
        Registrar
      </h2>

      <form @submit.prevent="register" class="space-y-4">

        <input
          v-model="email"
          type="email"
          placeholder="Email"
          required
          class="w-full px-4 py-3 border border-gray-300 rounded-lg focus:outline-none focus:ring-2 focus:ring-green-500"
        />

        <input
          v-model="password"
          type="password"
          placeholder="Senha"
          required
          class="w-full px-4 py-3 border border-gray-300 rounded-lg focus:outline-none focus:ring-2 focus:ring-green-500"
        />

        <button
          type="submit"
          :disabled="loading"
          class="w-full py-3 bg-green-600 text-white font-medium rounded-lg hover:bg-green-700 transition disabled:opacity-60 disabled:cursor-not-allowed"
        >
          {{ loading ? 'Criando...' : 'Criar Conta' }}
        </button>

        <p
          v-if="message"
          class="text-green-600 text-sm text-center mt-2"
        >
          {{ message }}
        </p>

        <p
          v-if="error"
          class="text-red-500 text-sm text-center mt-2"
        >
          {{ error }}
        </p>

      </form>

      <!-- Divider -->
      <div class="relative my-8">
        <div class="absolute inset-0 flex items-center">
          <div class="w-full border-t border-gray-300"></div>
        </div>
      </div>

      <RouterLink
        to="/login"
        class="block w-full text-center py-3 bg-slate-800 text-white font-medium rounded-lg hover:bg-slate-700 transition"
      >
        Já tem conta? Login
      </RouterLink>

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

      message.value = 'Conta criada! Verifique seu email.'
      setTimeout(() => router.push('/login'), 2000)
    }

    return { email, password, register, loading, error, message }
  }
}
</script>