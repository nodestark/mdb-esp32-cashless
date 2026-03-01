<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Badge } from '@/components/ui/badge'

const supabase = useSupabaseClient()
const { role } = useOrganization()
const router = useRouter()

const isAdmin = computed(() => role.value === 'admin')

// Redirect non-admins
watch(role, (r) => {
  if (r && r !== 'admin') router.replace('/')
}, { immediate: true })

interface EmbeddedDevice {
  id: string
  created_at: string
  subdomain: number
  mac_address: string | null
  status: string
  status_at: string
  firmware_version: string | null
  firmware_build_date: string | null
  machine_name: string | null
  machine_id: string | null
}

const devices = ref<EmbeddedDevice[]>([])
const loading = ref(true)

async function fetchDevices() {
  loading.value = true
  try {
    // Fetch all embedded devices with their linked vendingMachine (if any)
    const { data, error } = await supabase
      .from('embeddeds')
      .select('id, created_at, subdomain, mac_address, status, status_at, firmware_version, firmware_build_date')
      .order('created_at', { ascending: false })

    if (error) throw error

    // Fetch machine assignments separately
    const { data: machines } = await supabase
      .from('vendingMachine')
      .select('id, name, embedded')
      .not('embedded', 'is', null)

    const machineMap = new Map<string, { id: string; name: string }>()
    for (const m of (machines ?? []) as any[]) {
      if (m.embedded) machineMap.set(m.embedded, { id: m.id, name: m.name })
    }

    devices.value = ((data ?? []) as any[]).map(d => ({
      ...d,
      machine_name: machineMap.get(d.id)?.name ?? null,
      machine_id: machineMap.get(d.id)?.id ?? null,
    }))
  } finally {
    loading.value = false
  }
}

onMounted(fetchDevices)

// ── Register Device modal ──────────────────────────────────────────────────
const showModal = ref(false)
const step = ref<1 | 2>(1)
const generating = ref(false)
const shortCode = ref('')
const expiresAt = ref('')
const genError = ref('')

function openModal() {
  step.value = 1
  shortCode.value = ''
  expiresAt.value = ''
  genError.value = ''
  showModal.value = true
}

async function generateCode() {
  generating.value = true
  genError.value = ''
  try {
    const { data, error } = await supabase.functions.invoke('create-provisioning-token', {
      body: { device_only: true },
    })
    if (error) throw error
    if (data?.error) throw new Error(data.error)
    shortCode.value = data.short_code
    expiresAt.value = new Date(data.expires_at).toLocaleTimeString()
    step.value = 2
  } catch (err: unknown) {
    genError.value = err instanceof Error ? err.message : 'Failed to generate code'
  } finally {
    generating.value = false
  }
}

function closeModal() {
  showModal.value = false
  if (step.value === 2) fetchDevices()
}

// ── Delete device ────────────────────────────────────────────────────────
const showDeleteModal = ref(false)
const deleteTarget = ref<EmbeddedDevice | null>(null)
const deleting = ref(false)
const deleteError = ref('')

function openDeleteModal(device: EmbeddedDevice) {
  deleteTarget.value = device
  deleteError.value = ''
  showDeleteModal.value = true
}

async function confirmDelete() {
  if (!deleteTarget.value) return
  deleting.value = true
  deleteError.value = ''
  try {
    const { error } = await supabase
      .from('embeddeds')
      .delete()
      .eq('id', deleteTarget.value.id)
    if (error) throw error
    showDeleteModal.value = false
    await fetchDevices()
  } catch (err: unknown) {
    deleteError.value = err instanceof Error ? err.message : 'Failed to delete device'
  } finally {
    deleting.value = false
  }
}

// ── Helpers ───────────────────────────────────────────────────────────────
function timeAgo(dt: string | null | undefined): string {
  if (!dt) return '—'
  const seconds = Math.floor((Date.now() - new Date(dt).getTime()) / 1000)
  if (seconds < 60) return 'just now'
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return `${hours}h ago`
  const days = Math.floor(hours / 24)
  return `${days}d ago`
}
</script>

<template>
  <div class="flex flex-1 flex-col gap-4 p-4 md:p-6">
        <div class="flex items-center justify-between">
          <h1 class="text-2xl font-semibold">Devices</h1>
          <button
            v-if="isAdmin"
            class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
            @click="openModal"
          >
            Register Device
          </button>
        </div>

        <div v-if="loading" class="text-muted-foreground">Loading devices...</div>

        <div v-else-if="devices.length === 0" class="text-muted-foreground">
          No embedded devices registered yet.
        </div>

        <div v-else class="rounded-md border">
          <table class="w-full text-sm">
            <thead>
              <tr class="border-b bg-muted/50 text-left">
                <th class="px-4 py-3 font-medium">Subdomain</th>
                <th class="px-4 py-3 font-medium">MAC Address</th>
                <th class="px-4 py-3 font-medium">Status</th>
                <th class="px-4 py-3 font-medium">Firmware</th>
                <th class="px-4 py-3 font-medium">Assigned Machine</th>
                <th class="px-4 py-3 font-medium">Last Seen</th>
                <th class="px-4 py-3 font-medium">Registered</th>
                <th class="px-4 py-3 font-medium">Actions</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="device in devices"
                :key="device.id"
                class="border-b last:border-0 hover:bg-muted/30 transition-colors"
              >
                <td class="px-4 py-3 font-mono">{{ device.subdomain }}</td>
                <td class="px-4 py-3 font-mono text-muted-foreground">
                  {{ device.mac_address ?? '—' }}
                </td>
                <td class="px-4 py-3">
                  <Badge
                    :variant="device.status === 'online' ? 'default' : device.status?.startsWith('ota_') ? 'default' : 'secondary'"
                  >
                    <span
                      class="mr-1 inline-block h-2 w-2 rounded-full"
                      :class="{
                        'bg-green-400': device.status === 'online',
                        'bg-yellow-400': device.status === 'ota_updating',
                        'bg-green-400 animate-pulse': device.status === 'ota_success',
                        'bg-red-400': device.status === 'ota_failed',
                        'bg-muted-foreground/50': !['online', 'ota_updating', 'ota_success', 'ota_failed'].includes(device.status),
                      }"
                    />
                    {{ device.status === 'ota_updating' ? 'updating' : device.status === 'ota_success' ? 'updated' : device.status === 'ota_failed' ? 'update failed' : device.status }}
                  </Badge>
                </td>
                <td class="px-4 py-3 text-muted-foreground">
                  <template v-if="device.firmware_version">
                    <span class="font-mono">{{ device.firmware_version }}</span>
                    <span v-if="device.firmware_build_date" class="ml-1 text-xs">
                      ({{ new Date(device.firmware_build_date).toLocaleString() }})
                    </span>
                  </template>
                  <span v-else>—</span>
                </td>
                <td class="px-4 py-3">
                  <NuxtLink
                    v-if="device.machine_id"
                    :to="`/machines/${device.machine_id}`"
                    class="text-primary hover:underline"
                  >
                    {{ device.machine_name }}
                  </NuxtLink>
                  <span v-else class="text-muted-foreground">Unassigned</span>
                </td>
                <td class="px-4 py-3 text-muted-foreground">
                  {{ timeAgo(device.status_at) }}
                </td>
                <td class="px-4 py-3 text-muted-foreground">
                  {{ new Date(device.created_at).toLocaleDateString() }}
                </td>
                <td class="px-4 py-3">
                  <button
                    class="text-xs text-destructive hover:underline"
                    @click.prevent="openDeleteModal(device)"
                  >
                    Delete
                  </button>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
  </div>

  <!-- Delete Confirmation Modal -->
  <div
    v-if="showDeleteModal"
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    @click.self="showDeleteModal = false"
  >
    <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
      <h2 class="mb-1 text-lg font-semibold">Delete device</h2>
      <p class="mb-4 text-sm text-muted-foreground">
        Are you sure you want to delete device
        <strong class="text-foreground">{{ deleteTarget?.mac_address ?? `subdomain ${deleteTarget?.subdomain}` }}</strong>?
      </p>
      <p v-if="deleteTarget?.machine_name" class="mb-4 text-sm text-muted-foreground">
        This device is currently assigned to <strong class="text-foreground">{{ deleteTarget.machine_name }}</strong>. The machine will be unassigned but its configuration and sales history will be preserved.
      </p>
      <p v-if="deleteError" class="mb-3 text-sm text-destructive">{{ deleteError }}</p>
      <div class="flex gap-2">
        <button
          class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium hover:bg-muted"
          @click="showDeleteModal = false"
        >
          Cancel
        </button>
        <button
          :disabled="deleting"
          class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-destructive px-4 text-sm font-medium text-destructive-foreground shadow hover:bg-destructive/90 disabled:opacity-50"
          @click="confirmDelete"
        >
          <span v-if="deleting">Deleting...</span>
          <span v-else>Delete</span>
        </button>
      </div>
    </div>
  </div>

  <!-- Register Device Modal -->
  <div
    v-if="showModal"
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    @click.self="closeModal"
  >
    <div class="w-full max-w-md rounded-xl border bg-card p-6 shadow-lg">
      <!-- Step 1: Generate code -->
      <template v-if="step === 1">
        <h2 class="mb-1 text-lg font-semibold">Register a Device</h2>
        <p class="mb-5 text-sm text-muted-foreground">
          Generate a one-time provisioning code for a new embedded device. The device will be registered without creating a vending machine — you can assign it to a machine later.
        </p>
        <p v-if="genError" class="mb-3 text-sm text-destructive">{{ genError }}</p>
        <div class="flex gap-2">
          <button
            class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium hover:bg-muted"
            @click="closeModal"
          >
            Cancel
          </button>
          <button
            :disabled="generating"
            class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow hover:bg-primary/90 disabled:opacity-50"
            @click="generateCode"
          >
            <span v-if="generating">Generating...</span>
            <span v-else>Generate Code</span>
          </button>
        </div>
      </template>

      <!-- Step 2: Show code + instructions -->
      <template v-else>
        <h2 class="mb-1 text-lg font-semibold">Provisioning Code</h2>
        <p class="mb-4 text-sm text-muted-foreground">Valid until {{ expiresAt }}. Single use.</p>

        <!-- Code display -->
        <div class="mb-5 rounded-lg border-2 border-dashed border-primary/40 bg-primary/5 py-4 text-center">
          <p class="font-mono text-4xl font-bold tracking-[0.3em] text-primary">{{ shortCode }}</p>
        </div>

        <!-- Instructions -->
        <ol class="mb-5 space-y-2 text-sm text-muted-foreground">
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">1</span>
            Connect your phone to the device's WiFi network: <strong class="text-foreground ml-1">VMflow</strong> (password: <strong class="text-foreground">12345678</strong>)
          </li>
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">2</span>
            Open <strong class="text-foreground">192.168.4.1</strong> in your browser
          </li>
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">3</span>
            Enter your WiFi credentials, the provisioning code above, and the server URL
          </li>
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">4</span>
            Save — the device will connect and register automatically, then restart
          </li>
        </ol>

        <button
          class="inline-flex h-9 w-full items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow hover:bg-primary/90"
          @click="closeModal"
        >
          Done
        </button>
      </template>
    </div>
  </div>
</template>
