export default defineNuxtPlugin({
  name: 'supabase-url',
  enforce: 'pre',
  order: -30, // run before @nuxtjs/supabase plugin (order -20)
  setup() {
    const config = useRuntimeConfig()

    const url = config.public.supabase.url as string
    if (!url) return

    // Replace localhost/127.0.0.1 with the actual browser hostname
    // so the app works when accessed from other machines on the LAN
    const parsed = new URL(url)
    if (parsed.hostname === '127.0.0.1' || parsed.hostname === 'localhost') {
      parsed.hostname = window.location.hostname
      config.public.supabase.url = parsed.toString().replace(/\/$/, '')
    }
  },
})
