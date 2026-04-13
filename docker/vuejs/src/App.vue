<template>
  <section class="content-body">
    <RouterView />
  </section>
</template>

<script>
import { supabase } from '@/lib/supabase'
import { useRouter } from 'vue-router'

export default {
  setup() {
    const router = useRouter()

    supabase.auth.onAuthStateChange((event) => {
      if (event === 'PASSWORD_RECOVERY') {
        router.push('/reset-password')
      }
    })
  }
}
</script>