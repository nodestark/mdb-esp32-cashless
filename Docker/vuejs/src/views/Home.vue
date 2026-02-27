<template>
  <div class="home">
    <h1>Bem-vindo</h1>
    <p>Projeto com autenticação Supabase + Vue 3</p>

    <div class="actions">
      <RouterLink to="/login" class="btn">Login</RouterLink>
      <RouterLink to="/register" class="btn secondary">Registrar</RouterLink>
      <RouterLink to="/dashboard" class="btn success">
        Ir para Dashboard
      </RouterLink>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Home'
}
</script>

<style scoped>
.home {
  text-align: center;
  margin-top: 120px;
}

.actions {
  margin-top: 30px;
}

.btn {
  margin: 10px;
  padding: 10px 20px;
  text-decoration: none;
  background: #2563eb;
  color: white;
  border-radius: 6px;
}

.secondary {
  background: #6b7280;
}

.success {
  background: #16a34a;
}
</style>