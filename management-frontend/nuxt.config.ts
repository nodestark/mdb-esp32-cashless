// https://nuxt.com/docs/api/configuration/nuxt-config
export default defineNuxtConfig({
  compatibilityDate: '2025-07-15',
  modules: ['@nuxtjs/supabase', '@nuxtjs/tailwindcss', 'shadcn-nuxt'],
  devtools: { enabled: true },
  supabase: {
    // URL and key are read from SUPABASE_URL / SUPABASE_KEY in .env
    redirect: false,
  },
})
