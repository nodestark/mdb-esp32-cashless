<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

const supabase = useSupabaseClient()
const { role } = useOrganization()

const isAdmin = computed(() => role.value === 'admin')

interface ApiKey {
  id: string
  name: string
  key_prefix: string
  created_at: string
  last_used_at: string | null
  revoked_at: string | null
}

const keys = ref<ApiKey[]>([])
const loading = ref(true)

async function fetchKeys() {
  loading.value = true
  const { data, error } = await supabase
    .from('api_keys')
    .select('id, name, key_prefix, created_at, last_used_at, revoked_at')
    .order('created_at', { ascending: false })
  if (!error) {
    keys.value = (data ?? []) as ApiKey[]
  }
  loading.value = false
}

onMounted(fetchKeys)

// Create key modal
const showCreateModal = ref(false)
const createName = ref('')
const createLoading = ref(false)
const createError = ref('')
const createdKey = ref('')
const copied = ref(false)

function openCreateModal() {
  createName.value = ''
  createError.value = ''
  createdKey.value = ''
  createLoading.value = false
  copied.value = false
  showCreateModal.value = true
}

async function submitCreate() {
  const name = createName.value.trim()
  if (!name) {
    createError.value = 'Name is required'
    return
  }
  createLoading.value = true
  createError.value = ''
  try {
    const { data, error } = await supabase.functions.invoke('create-api-key', {
      body: { name },
    })
    if (error) throw error
    if (data?.error) throw new Error(data.error)
    createdKey.value = data.key
    await fetchKeys()
  } catch (err: unknown) {
    createError.value = err instanceof Error ? err.message : 'Failed to create API key'
  } finally {
    createLoading.value = false
  }
}

async function copyKey() {
  try {
    await navigator.clipboard.writeText(createdKey.value)
    copied.value = true
    setTimeout(() => { copied.value = false }, 2000)
  } catch {
    const textarea = document.createElement('textarea')
    textarea.value = createdKey.value
    document.body.appendChild(textarea)
    textarea.select()
    document.execCommand('copy')
    document.body.removeChild(textarea)
    copied.value = true
    setTimeout(() => { copied.value = false }, 2000)
  }
}

async function revokeKey(id: string) {
  await supabase
    .from('api_keys')
    .update({ revoked_at: new Date().toISOString() })
    .eq('id', id)
  await fetchKeys()
}

function formatDate(dt: string | null) {
  if (!dt) return '—'
  return new Date(dt).toLocaleDateString()
}

function timeAgo(dt: string | null) {
  if (!dt) return 'Never'
  const diff = Date.now() - new Date(dt).getTime()
  const mins = Math.floor(diff / 60000)
  if (mins < 1) return 'just now'
  if (mins < 60) return `${mins}m ago`
  const hrs = Math.floor(mins / 60)
  if (hrs < 24) return `${hrs}h ago`
  const days = Math.floor(hrs / 24)
  return `${days}d ago`
}
</script>

<template>
  <div class="flex flex-1 flex-col gap-6 p-4 md:p-6">
    <div class="flex items-center justify-between">
      <div>
        <h1 class="text-2xl font-semibold">API Keys</h1>
        <p class="mt-1 text-sm text-muted-foreground">Manage API keys for external integrations. Use these keys to call the API from your own applications.</p>
      </div>
      <button
        v-if="isAdmin"
        class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
        @click="openCreateModal"
      >
        Create API Key
      </button>
    </div>

    <div v-if="loading" class="text-muted-foreground">Loading…</div>

    <template v-else>
      <div v-if="keys.length === 0" class="text-sm text-muted-foreground">No API keys created yet.</div>
      <div v-else class="rounded-md border">
        <table class="w-full text-sm">
          <thead>
            <tr class="border-b bg-muted/50 text-left">
              <th class="px-4 py-3 font-medium">Name</th>
              <th class="px-4 py-3 font-medium">Key</th>
              <th class="px-4 py-3 font-medium">Created</th>
              <th class="px-4 py-3 font-medium">Last used</th>
              <th class="px-4 py-3 font-medium">Status</th>
              <th v-if="isAdmin" class="px-4 py-3 font-medium">Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="key in keys"
              :key="key.id"
              class="border-b last:border-0 transition-colors hover:bg-muted/30"
              :class="key.revoked_at ? 'opacity-50' : ''"
            >
              <td class="px-4 py-3 font-medium">{{ key.name }}</td>
              <td class="px-4 py-3">
                <code class="rounded bg-muted px-1.5 py-0.5 text-xs">{{ key.key_prefix }}…</code>
              </td>
              <td class="px-4 py-3 text-muted-foreground">{{ formatDate(key.created_at) }}</td>
              <td class="px-4 py-3 text-muted-foreground">{{ timeAgo(key.last_used_at) }}</td>
              <td class="px-4 py-3">
                <span
                  class="rounded-full px-2 py-0.5 text-xs font-medium"
                  :class="key.revoked_at
                    ? 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400'
                    : 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400'"
                >
                  {{ key.revoked_at ? 'Revoked' : 'Active' }}
                </span>
              </td>
              <td v-if="isAdmin" class="px-4 py-3">
                <button
                  v-if="!key.revoked_at"
                  class="text-xs text-destructive hover:underline"
                  @click="revokeKey(key.id)"
                >
                  Revoke
                </button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- Usage instructions -->
      <div class="rounded-xl border bg-card p-6">
        <h2 class="mb-2 text-base font-medium">Usage</h2>
        <p class="mb-3 text-sm text-muted-foreground">
          Use your API key with the <code class="rounded bg-muted px-1 py-0.5 text-xs">X-API-Key</code> header to authenticate requests.
        </p>
        <div class="rounded-md bg-muted p-4">
          <pre class="overflow-x-auto text-xs"><code>curl -X POST {{ useRuntimeConfig().public.supabase?.url ?? 'https://your-supabase-url' }}/functions/v1/send-credit \
  -H "X-API-Key: vmf_your_api_key_here" \
  -H "Content-Type: application/json" \
  -d '{"device_id": "your-device-uuid", "amount": 1.50}'</code></pre>
        </div>
      </div>
    </template>
  </div>

  <!-- Create API key modal -->
  <div
    v-if="showCreateModal"
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    @click.self="showCreateModal = false"
  >
    <div class="w-full max-w-md rounded-xl border bg-card p-6 shadow-lg">
      <!-- Step 1: Name form -->
      <template v-if="!createdKey">
        <h2 class="mb-4 text-lg font-semibold">Create API Key</h2>
        <form class="space-y-4" @submit.prevent="submitCreate">
          <div class="space-y-1">
            <label class="text-sm font-medium" for="key-name">Name</label>
            <input
              id="key-name"
              v-model="createName"
              type="text"
              required
              placeholder="e.g. Production integration"
              class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
            />
          </div>
          <p v-if="createError" class="text-sm text-destructive">{{ createError }}</p>
          <div class="flex gap-2">
            <button
              type="button"
              class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
              @click="showCreateModal = false"
            >
              Cancel
            </button>
            <button
              type="submit"
              :disabled="createLoading"
              class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
            >
              <span v-if="createLoading">Creating…</span>
              <span v-else>Create</span>
            </button>
          </div>
        </form>
      </template>

      <!-- Step 2: Show key -->
      <template v-else>
        <h2 class="mb-1 text-lg font-semibold">API Key Created</h2>
        <p class="mb-4 text-sm text-destructive font-medium">
          Copy this key now. It will not be shown again.
        </p>

        <div class="mb-4 flex items-stretch gap-2">
          <div class="flex-1 overflow-hidden rounded-md border border-input bg-muted/50 px-3 py-2">
            <p class="truncate font-mono text-xs text-muted-foreground">{{ createdKey }}</p>
          </div>
          <button
            class="inline-flex shrink-0 items-center justify-center rounded-md bg-primary px-3 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
            @click="copyKey"
          >
            {{ copied ? 'Copied!' : 'Copy' }}
          </button>
        </div>

        <button
          class="inline-flex h-9 w-full items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
          @click="showCreateModal = false; createdKey = ''"
        >
          Done
        </button>
      </template>
    </div>
  </div>
</template>
