<template>
  <div>
    <h1>Vending Machines</h1>

    <div class="grid">
      <div class="machine" v-for="n in 8" :key="n">
        <strong>VM-{{ n }}</strong>
        <span>Status: Online</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 16px;
}

.machine {
  background: white;
  padding: 16px;
  border-radius: 8px;
}
</style>