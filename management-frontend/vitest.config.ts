import { defineConfig } from 'vitest/config'
import vue from '@vitejs/plugin-vue'
import { fileURLToPath } from 'node:url'
import { resolve, dirname } from 'node:path'

const __dirname = dirname(fileURLToPath(import.meta.url))

export default defineConfig({
  plugins: [vue()],
  test: {
    environment: 'happy-dom',
    include: ['app/**/*.test.ts', 'app/**/*.spec.ts'],
  },
  resolve: {
    alias: {
      '#imports': resolve(__dirname, 'app/test-helpers/nuxt-stubs.ts'),
    },
  },
})
