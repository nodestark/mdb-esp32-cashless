<script setup lang="ts">
definePageMeta({ layout: false })

const supabase = useSupabaseClient()
const name = ref('')
const loading = ref(false)
const errorMsg = ref('')

async function createOrganization() {
  loading.value = true
  errorMsg.value = ''
  try {
    const { data, error } = await supabase.functions.invoke('create-organization', {
      body: { name: name.value },
    })
    if (error) throw error
    if (data?.error) throw new Error(data.error)
    await navigateTo('/')
  } catch (err: unknown) {
    errorMsg.value = err instanceof Error ? err.message : 'An error occurred'
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="flex min-h-screen items-center justify-center bg-background">
    <div class="w-full max-w-sm">
      <div class="rounded-xl border bg-card p-8 shadow-sm">
        <div class="mb-6 text-center">
          <h1 class="text-2xl font-semibold">Create your organization</h1>
          <p class="mt-1 text-sm text-muted-foreground">You'll be set as the admin</p>
        </div>

        <form class="space-y-4" @submit.prevent="createOrganization">
          <div class="space-y-1">
            <label class="text-sm font-medium" for="org-name">Organization name</label>
            <input
              id="org-name"
              v-model="name"
              type="text"
              required
              placeholder="Acme Corp"
              class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
            />
          </div>

          <p v-if="errorMsg" class="text-sm text-destructive">{{ errorMsg }}</p>

          <button
            type="submit"
            :disabled="loading"
            class="inline-flex h-9 w-full items-center justify-center rounded-md bg-primary px-4 py-2 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:pointer-events-none disabled:opacity-50"
          >
            <span v-if="loading">Creating…</span>
            <span v-else>Create organization</span>
          </button>
        </form>
      </div>
    </div>
  </div>
</template>
