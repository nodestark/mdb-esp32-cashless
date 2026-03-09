// https://nuxt.com/docs/api/configuration/nuxt-config
export default defineNuxtConfig({
  compatibilityDate: '2025-07-15',
  modules: ['@nuxtjs/supabase', '@nuxtjs/tailwindcss', 'shadcn-nuxt', '@vite-pwa/nuxt'],
  devtools: { enabled: true },
  app: {
    head: {
      meta: [
        { name: 'viewport', content: 'width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover' },
        { name: 'apple-mobile-web-app-capable', content: 'yes' },
        { name: 'apple-mobile-web-app-status-bar-style', content: 'default' },
        { name: 'apple-mobile-web-app-title', content: 'VMflow' },
        { name: 'theme-color', content: '#ffffff', media: '(prefers-color-scheme: light)' },
        { name: 'theme-color', content: '#09090b', media: '(prefers-color-scheme: dark)' },
      ],
      link: [],
    },
  },
  supabase: {
    // URL and key are read from SUPABASE_URL / SUPABASE_KEY in .env
    redirect: false,
    cookieOptions: {
      // Allow cookies over plain HTTP during local/LAN development
      secure: false,
    },
  },
  routeRules: {
    // Proxy Supabase API requests so LAN devices (ESP32) can reach Supabase
    // through the Nuxt dev server when the Supabase port is firewalled
    '/functions/**': { proxy: 'http://127.0.0.1:54321/functions/**' },
  },
  runtimeConfig: {
    public: {
      vapidPublicKey: process.env.VAPID_PUBLIC_KEY ?? '',
      githubFirmwareRepo: process.env.GITHUB_FIRMWARE_REPO ?? '',
    },
  },
  pwa: {
    strategies: 'injectManifest',
    srcDir: 'service-worker',
    filename: 'sw.ts',
    registerType: 'autoUpdate',
    injectRegister: 'auto',
    manifest: {
      name: 'VMflow',
      short_name: 'VMflow',
      description: 'Vending machine management dashboard',
      theme_color: '#3b82f6',
      background_color: '#ffffff',
      display: 'standalone',
      start_url: '/',
      icons: [],
      shortcuts: [
        { name: 'Machines', url: '/machines' },
        { name: 'Warehouse', url: '/warehouse' },
      ],
    },
    injectManifest: {
      globPatterns: ['**/*.{js,css,html,png,svg,ico,woff2}'],
    },
    devOptions: {
      enabled: true,
      type: 'module',
    },
  },
})
