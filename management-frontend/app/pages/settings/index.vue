<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { IconMoon, IconSun } from '@tabler/icons-vue'

const supabase = useSupabaseClient()
const user = useSupabaseUser()
const { organization, role } = useOrganization()
const { isDark, toggleTheme } = useTheme()

// ── Profile info ─────────────────────────────────────────────────────────────
// @nuxtjs/supabase v2 returns JWT claims (sub) not User object (id)
const userId = computed(() => user.value?.id ?? (user.value as any)?.sub ?? null)
const email = computed(() => user.value?.email ?? '')
const createdAt = computed(() => {
  if (!user.value?.created_at) return '—'
  return new Date(user.value.created_at).toLocaleDateString()
})

// ── Name editing ─────────────────────────────────────────────────────────────
const firstName = ref('')
const lastName = ref('')
const nameLoading = ref(false)
const nameError = ref('')
const nameSuccess = ref('')

async function loadProfile() {
  if (!userId.value) return
  const { data } = await supabase
    .from('users')
    .select('first_name, last_name')
    .eq('id', userId.value)
    .single()
  if (data) {
    firstName.value = (data as any).first_name ?? ''
    lastName.value = (data as any).last_name ?? ''
  }
}

async function saveName() {
  nameError.value = ''
  nameSuccess.value = ''
  if (!userId.value) return

  nameLoading.value = true
  try {
    const { error } = await supabase
      .from('users')
      .update({ first_name: firstName.value || null, last_name: lastName.value || null })
      .eq('id', userId.value)
    if (error) throw error
    nameSuccess.value = 'Name updated successfully'
  } catch (err: unknown) {
    nameError.value = err instanceof Error ? err.message : 'Failed to update name'
  } finally {
    nameLoading.value = false
  }
}

watch(userId, (uid) => { if (import.meta.client && uid) loadProfile() }, { immediate: true })

// ── Password change ──────────────────────────────────────────────────────────
const newPassword = ref('')
const confirmPassword = ref('')
const passwordLoading = ref(false)
const passwordError = ref('')
const passwordSuccess = ref('')

async function changePassword() {
  passwordError.value = ''
  passwordSuccess.value = ''

  if (newPassword.value.length < 6) {
    passwordError.value = 'Password must be at least 6 characters'
    return
  }
  if (newPassword.value !== confirmPassword.value) {
    passwordError.value = 'Passwords do not match'
    return
  }

  passwordLoading.value = true
  try {
    const { error } = await supabase.auth.updateUser({
      password: newPassword.value,
    })
    if (error) throw error
    passwordSuccess.value = 'Password updated successfully'
    newPassword.value = ''
    confirmPassword.value = ''
  } catch (err: unknown) {
    passwordError.value = err instanceof Error ? err.message : 'Failed to update password'
  } finally {
    passwordLoading.value = false
  }
}

// ── Email change ─────────────────────────────────────────────────────────────
const newEmail = ref('')
const emailLoading = ref(false)
const emailError = ref('')
const emailSuccess = ref('')

async function changeEmail() {
  emailError.value = ''
  emailSuccess.value = ''

  if (!newEmail.value || !newEmail.value.includes('@')) {
    emailError.value = 'Please enter a valid email address'
    return
  }

  emailLoading.value = true
  try {
    const { error } = await supabase.auth.updateUser({
      email: newEmail.value,
    })
    if (error) throw error
    emailSuccess.value = 'Confirmation email sent to your new address. Please check your inbox.'
    newEmail.value = ''
  } catch (err: unknown) {
    emailError.value = err instanceof Error ? err.message : 'Failed to update email'
  } finally {
    emailLoading.value = false
  }
}
</script>

<template>
  <div class="flex flex-1 flex-col gap-6 p-4 md:p-6">
        <h1 class="text-2xl font-semibold">Account Settings</h1>

        <div class="grid gap-6 md:max-w-2xl">
          <!-- Profile Information -->
          <div class="rounded-xl border bg-card p-6 shadow-sm">
            <h2 class="mb-1 text-lg font-semibold">Profile</h2>
            <p class="mb-5 text-sm text-muted-foreground">Your account information.</p>

            <form class="space-y-4" @submit.prevent="saveName">
              <div class="grid grid-cols-2 gap-3">
                <div class="space-y-1">
                  <label class="text-sm font-medium" for="settings-first-name">First name</label>
                  <input
                    id="settings-first-name"
                    v-model="firstName"
                    type="text"
                    placeholder="First name"
                    class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                  />
                </div>
                <div class="space-y-1">
                  <label class="text-sm font-medium" for="settings-last-name">Last name</label>
                  <input
                    id="settings-last-name"
                    v-model="lastName"
                    type="text"
                    placeholder="Last name"
                    class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                  />
                </div>
              </div>

              <div class="space-y-1">
                <label class="text-sm font-medium">Email</label>
                <p class="text-sm text-muted-foreground">{{ email }}</p>
              </div>
              <div class="space-y-1">
                <label class="text-sm font-medium">Organisation</label>
                <p class="text-sm text-muted-foreground">
                  {{ organization?.name ?? '—' }}
                  <span
                    v-if="role"
                    class="ml-2 rounded-full px-2 py-0.5 text-xs font-medium"
                    :class="role === 'admin' ? 'bg-primary/10 text-primary' : 'bg-muted text-muted-foreground'"
                  >
                    {{ role }}
                  </span>
                </p>
              </div>
              <div class="space-y-1">
                <label class="text-sm font-medium">Account created</label>
                <p class="text-sm text-muted-foreground">{{ createdAt }}</p>
              </div>

              <p v-if="nameError" class="text-sm text-destructive">{{ nameError }}</p>
              <p v-if="nameSuccess" class="text-sm text-green-600">{{ nameSuccess }}</p>

              <button
                type="submit"
                :disabled="nameLoading"
                class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="nameLoading">Saving…</span>
                <span v-else>Save name</span>
              </button>
            </form>
          </div>

          <!-- Change Email -->
          <div class="rounded-xl border bg-card p-6 shadow-sm">
            <h2 class="mb-1 text-lg font-semibold">Change Email</h2>
            <p class="mb-5 text-sm text-muted-foreground">
              Update your email address. A confirmation link will be sent to the new address.
            </p>

            <form class="space-y-4" @submit.prevent="changeEmail">
              <div class="space-y-1">
                <label class="text-sm font-medium" for="new-email">New email address</label>
                <input
                  id="new-email"
                  v-model="newEmail"
                  type="email"
                  required
                  placeholder="new@example.com"
                  class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                />
              </div>

              <p v-if="emailError" class="text-sm text-destructive">{{ emailError }}</p>
              <p v-if="emailSuccess" class="text-sm text-green-600">{{ emailSuccess }}</p>

              <button
                type="submit"
                :disabled="emailLoading"
                class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="emailLoading">Updating…</span>
                <span v-else>Update email</span>
              </button>
            </form>
          </div>

          <!-- Change Password -->
          <div class="rounded-xl border bg-card p-6 shadow-sm">
            <h2 class="mb-1 text-lg font-semibold">Change Password</h2>
            <p class="mb-5 text-sm text-muted-foreground">
              Update your password. Use at least 6 characters.
            </p>

            <form class="space-y-4" @submit.prevent="changePassword">
              <div class="space-y-1">
                <label class="text-sm font-medium" for="new-password">New password</label>
                <input
                  id="new-password"
                  v-model="newPassword"
                  type="password"
                  required
                  placeholder="Enter new password"
                  class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                />
              </div>
              <div class="space-y-1">
                <label class="text-sm font-medium" for="confirm-password">Confirm new password</label>
                <input
                  id="confirm-password"
                  v-model="confirmPassword"
                  type="password"
                  required
                  placeholder="Confirm new password"
                  class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                />
              </div>

              <p v-if="passwordError" class="text-sm text-destructive">{{ passwordError }}</p>
              <p v-if="passwordSuccess" class="text-sm text-green-600">{{ passwordSuccess }}</p>

              <button
                type="submit"
                :disabled="passwordLoading"
                class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="passwordLoading">Updating…</span>
                <span v-else>Update password</span>
              </button>
            </form>
          </div>

          <!-- Appearance -->
          <div class="rounded-xl border bg-card p-6 shadow-sm">
            <h2 class="mb-1 text-lg font-semibold">Appearance</h2>
            <p class="mb-5 text-sm text-muted-foreground">Customize the look and feel of the dashboard.</p>

            <div class="flex items-center justify-between">
              <div class="space-y-0.5">
                <label class="text-sm font-medium">Dark mode</label>
                <p class="text-sm text-muted-foreground">
                  {{ isDark ? 'Dark theme is active' : 'Light theme is active' }}
                </p>
              </div>
              <button
                class="inline-flex h-9 w-9 items-center justify-center rounded-md border border-input bg-background shadow-sm transition-colors hover:bg-muted"
                @click="toggleTheme()"
              >
                <IconMoon v-if="isDark" class="size-4" />
                <IconSun v-else class="size-4" />
              </button>
            </div>
          </div>
        </div>
      </div>
</template>
