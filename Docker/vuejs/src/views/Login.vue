<template>
  <div class="login-container">
    <h2>Login</h2>

    <form @submit.prevent="login">
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
        {{ loading ? 'Entrando...' : 'Entrar' }}
      </button>

      <p v-if="error" class="error">{{ error }}</p>
    </form>

    <!-- Separador -->
    <div class="divider">
      <span>ou</span>
    </div>

    <!-- Botão Registrar -->
    <RouterLink to="/register" class="register-btn">
      Criar uma conta
    </RouterLink>

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

<style scoped>
.login-container {
  max-width: 400px;
  margin: 100px auto;
  display: flex;
  flex-direction: column;
}

input {
  margin-bottom: 10px;
  padding: 10px;
  border: 1px solid #ddd;
  border-radius: 4px;
}

button {
  padding: 10px;
  cursor: pointer;
  background: #2563eb;
  color: white;
  border: none;
  border-radius: 4px;
}

button:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.error {
  color: red;
  margin-top: 8px;
}

.divider {
  text-align: center;
  margin: 20px 0;
  position: relative;
}

.divider span {
  background: white;
  padding: 0 10px;
  color: #888;
}

.register-btn {
  text-align: center;
  padding: 10px;
  background: #16a34a;
  color: white;
  text-decoration: none;
  border-radius: 4px;
}

.register-btn:hover {
  background: #15803d;
}
</style>