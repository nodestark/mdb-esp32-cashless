<script setup lang="ts">
definePageMeta({ layout: false })

const route = useRoute()
const supabase = useSupabaseClient()
const loading = ref(true)
const errorMsg = ref('')
const success = ref(false)

onMounted(async () => {
  const token = route.query.token as string
  if (!token) {
    errorMsg.value = 'No invitation token provided'
    loading.value = false
    return
  }

  try {
    const { data, error } = await supabase.functions.invoke('accept-invitation', {
      body: { token },
    })
    if (error) throw error
    if (data?.error) throw new Error(data.error)
    success.value = true
    setTimeout(() => navigateTo('/'), 2000)
  } catch (err: unknown) {
    errorMsg.value = err instanceof Error ? err.message : 'An error occurred'
  } finally {
    loading.value = false
  }
})
</script>

<template>
  <div class="flex min-h-screen items-center justify-center bg-background">
    <div class="w-full max-w-sm">
      <div class="rounded-xl border bg-card p-8 shadow-sm text-center">
        <div v-if="loading">
          <p class="text-muted-foreground">Accepting invitation…</p>
        </div>
        <div v-else-if="success">
          <h1 class="text-2xl font-semibold text-green-600">Welcome!</h1>
          <p class="mt-2 text-sm text-muted-foreground">You've joined the organization. Redirecting…</p>
        </div>
        <div v-else>
          <h1 class="text-2xl font-semibold">Invitation error</h1>
          <p class="mt-2 text-sm text-destructive">{{ errorMsg }}</p>
          <NuxtLink to="/auth/login" class="mt-4 inline-block text-sm text-primary underline-offset-4 hover:underline">
            Go to login
          </NuxtLink>
        </div>
      </div>
    </div>
  </div>
</template>
