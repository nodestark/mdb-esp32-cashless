<template>
  <div class="login">
    <h1>Login</h1>

    <input v-model="email" placeholder="Email" />
    <input v-model="password" type="password" placeholder="Senha" />

    <button @click="login">Entrar</button>

    <p v-if="error">{{ error }}</p>
  </div>
</template>

<script>
import { supabase } from '../lib/supabase'

export default {
  data() {
    return {
      email: '',
      password: '',
      error: null
    }
  },
  methods: {
    async login() {
      const { error } = await supabase.auth.signInWithPassword({
        email: this.email,
        password: this.password
      })

      if (error) {
        this.error = error.message
      } else {
        window.location.reload()
      }
    }
  }
}
</script>