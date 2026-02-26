<template>
  <div class="layout">
    <aside class="sidebar">
      <div class="brand">
        <span class="brand-icon">▣</span>
        <h2 class="logo">vmflow</h2>
      </div>

      <nav class="menu">
        <RouterLink to="/" class="menu-item">
          Dashboard
        </RouterLink>

        <RouterLink to="/embedded" class="menu-item">
          Embedded
        </RouterLink>
      </nav>

      <footer class="sidebar-footer">
        <small>vmflow © 2026</small>
      </footer>
    </aside>

    <main class="content">
      <div class="content-header">
        <h1>Dashboard</h1>
      </div>

      <section class="content-body">
        <RouterView />
      </section>
    </main>
  </div>
</template>

<style scoped>
/* Layout geral */
.layout {
  display: flex;
  min-height: 100vh;
  font-family: Inter, system-ui, sans-serif;
}

/* Sidebar */
.sidebar {
  width: 260px;
  background: linear-gradient(180deg, #020617, #020617);
  color: #e5e7eb;
  display: flex;
  flex-direction: column;
  padding: 20px;
  box-shadow: inset -1px 0 0 rgba(255, 255, 255, 0.05);
}

/* Marca */
.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 32px;
}

.brand-icon {
  background: #2563eb;
  color: white;
  font-size: 14px;
  padding: 6px 8px;
  border-radius: 6px;
}

.logo {
  font-size: 18px;
  font-weight: 600;
}

/* Menu */
.menu {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.menu-item {
  padding: 10px 12px;
  border-radius: 8px;
  color: #cbd5f5;
  text-decoration: none;
  font-size: 14px;
  transition: background 0.15s ease, color 0.15s ease;
}

.menu-item:hover {
  background: rgba(255, 255, 255, 0.08);
  color: #fff;
}

/* RouterLink ativo */
.menu-item.router-link-active {
  background: rgba(37, 99, 235, 0.18);
  color: #fff;
}

/* Footer da sidebar */
.sidebar-footer {
  margin-top: auto;
  font-size: 12px;
  color: #94a3b8;
}

/* Conteúdo */
.content {
  flex: 1;
  background: #f8fafc;
  display: flex;
  flex-direction: column;
}

.content-header {
  height: 64px;
  background: #ffffff;
  border-bottom: 1px solid #e5e7eb;
  display: flex;
  align-items: center;
  padding: 0 24px;
}

.content-header h1 {
  font-size: 18px;
  font-weight: 600;
  color: #0f172a;
}

.content-body {
  padding: 24px;
}
</style>