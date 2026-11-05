import { ref, computed, watch } from 'vue'

// Light/dark theme. The choice is kept per browser; with none saved, the OS
// preference decides. index.html applies the same rule before Vue loads, so
// the page never flashes the wrong theme.
const THEME_KEY = 'vmflow_theme'

function saved() {
  try { return localStorage.getItem(THEME_KEY) } catch { return null }
}

function systemPrefersDark() {
  return window.matchMedia?.('(prefers-color-scheme: dark)').matches ?? false
}

export const theme = ref(saved() ?? (systemPrefersDark() ? 'dark' : 'light'))
export const isDark = computed(() => theme.value === 'dark')

function apply(value) {
  document.documentElement.classList.toggle('dark', value === 'dark')
}

apply(theme.value)

watch(theme, apply)

// Follow the OS while the user has not picked a theme.
window.matchMedia?.('(prefers-color-scheme: dark)').addEventListener?.('change', e => {
  if (!saved()) theme.value = e.matches ? 'dark' : 'light'
})

export function toggleTheme() {
  theme.value = isDark.value ? 'light' : 'dark'
  try { localStorage.setItem(THEME_KEY, theme.value) } catch { /* private mode */ }
}
