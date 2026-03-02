<script setup lang="ts">
definePageMeta({ layout: false })

const supabase = useSupabaseClient()
const route = useRoute()

const email = ref('')
const password = ref('')
const loading = ref(false)
const errorMsg = ref('')
const statusMsg = ref('')

const inviteToken = computed(() => (route.query.token as string) || '')
const registerLink = computed(() =>
  inviteToken.value ? `/auth/register?token=${inviteToken.value}` : '/auth/register'
)

async function login() {
  loading.value = true
  errorMsg.value = ''
  statusMsg.value = ''

  const { error } = await supabase.auth.signInWithPassword({
    email: email.value,
    password: password.value,
  })

  if (error) {
    loading.value = false
    errorMsg.value = error.message
    return
  }

  // If there's an invitation token, accept it automatically
  if (inviteToken.value) {
    statusMsg.value = 'Joining organization…'
    try {
      const { data, error: inviteError } = await supabase.functions.invoke('accept-invitation', {
        body: { token: inviteToken.value },
      })
      if (inviteError) throw inviteError
      if (data?.error) throw new Error(data.error)
    } catch (err: unknown) {
      // Non-fatal: user is logged in, just couldn't auto-join
      // They can still accept manually via /onboarding/accept-invitation
      loading.value = false
      errorMsg.value = err instanceof Error ? err.message : 'Failed to join organization'
      return
    }
  }

  loading.value = false
  await navigateTo('/')
}
</script>

<template>
  <div class="flex min-h-screen items-center justify-center bg-background">
    <div class="w-full max-w-sm">
      <div class="rounded-xl border bg-card p-8 shadow-sm">
        <div class="mb-6 text-center">
          <h1 class="text-2xl font-semibold">Sign in</h1>
          <p class="mt-1 text-sm text-muted-foreground">Enter your email and password</p>
        </div>

        <!-- Invitation banner -->
        <div v-if="inviteToken" class="mb-5 rounded-lg border border-primary/30 bg-primary/5 px-4 py-3 text-center text-sm text-primary">
          Sign in to join the organization you've been invited to.
        </div>

        <form class="space-y-4" @submit.prevent="login">
          <div class="space-y-1">
            <label class="text-sm font-medium" for="email">Email</label>
            <input
              id="email"
              v-model="email"
              type="email"
              required
              autocomplete="email"
              placeholder="you@example.com"
              class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
            />
          </div>

          <div class="space-y-1">
            <label class="text-sm font-medium" for="password">Password</label>
            <input
              id="password"
              v-model="password"
              type="password"
              required
              autocomplete="current-password"
              placeholder="••••••••"
              class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
            />
          </div>

          <p v-if="errorMsg" class="text-sm text-destructive">{{ errorMsg }}</p>
          <p v-if="statusMsg" class="text-sm text-muted-foreground">{{ statusMsg }}</p>

          <button
            type="submit"
            :disabled="loading"
            class="inline-flex h-9 w-full items-center justify-center rounded-md bg-primary px-4 py-2 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:pointer-events-none disabled:opacity-50"
          >
            <span v-if="loading && statusMsg">{{ statusMsg }}</span>
            <span v-else-if="loading">Signing in…</span>
            <span v-else>Sign in</span>
          </button>
        </form>

        <p class="mt-4 text-center text-sm text-muted-foreground">
          Don't have an account?
          <NuxtLink :to="registerLink" class="text-primary underline-offset-4 hover:underline">Register</NuxtLink>
        </p>
      </div>
    </div>
  </div>
</template>
