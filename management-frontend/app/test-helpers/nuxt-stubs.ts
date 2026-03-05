/**
 * Stubs for Nuxt auto-imports used in composables.
 * Vitest resolves `#imports` to this file via the alias in vitest.config.ts.
 */
import { ref, computed, onMounted, onUnmounted, watch, reactive } from 'vue'

// Re-export Vue reactivity primitives that Nuxt normally auto-imports
export { ref, computed, onMounted, onUnmounted, watch, reactive }

// Stub useSupabaseClient — tests provide their own mock
export function useSupabaseClient() {
  throw new Error('useSupabaseClient must be mocked in tests')
}

// Stub useSupabaseUser
export function useSupabaseUser() {
  return ref(null)
}

// Stub useState (simplified — returns a plain ref)
export function useState<T>(key: string, init?: () => T) {
  return ref(init ? init() : undefined) as ReturnType<typeof ref<T>>
}
