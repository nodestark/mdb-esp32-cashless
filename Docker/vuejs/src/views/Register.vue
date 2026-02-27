<template>
  <div class="auth-container">
    <h2>Registrar</h2>

    <form @submit.prevent="register">
      <input
        v-model="email"
        type="email"
        placeholder="Email"
        required
      />

      <input
        v-model="password"
        type="password"
        placeholder="Senha"
        required
      />

      <button type="submit" :disabled="loading">
        {{ loading ? 'Criando...' : 'Criar Conta' }}
      </button>

      <p v-if="message" class="message">{{ message }}</p>
      <p v-if="error" class="error">{{ error }}</p>
    </form>

    <RouterLink to="/login">Já tem conta? Login</RouterLink>
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

<style scoped>
.auth-container {
  max-width: 400px;
  margin: 100px auto;
  display: flex;
  flex-direction: column;
}

input {
  margin-bottom: 10px;
  padding: 8px;
}

button {
  padding: 8px;
}

.error {
  color: red;
}

.message {
  color: green;
}
</style>