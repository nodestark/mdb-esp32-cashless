// https://nuxt.com/docs/api/configuration/nuxt-config
export default defineNuxtConfig({
  compatibilityDate: '2025-07-15',
  modules: ['@nuxtjs/supabase', '@nuxtjs/tailwindcss', 'shadcn-nuxt'],
  devtools: { enabled: true },
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
})
