<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Badge } from '@/components/ui/badge'
import QRCode from 'qrcode'
import { timeAgo } from '@/lib/utils'

const supabase = useSupabaseClient()
const { role } = useOrganization()
const router = useRouter()
const { pendingTokens, fetchPendingTokens, deletePendingToken } = useMachines()

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

onMounted(() => {
  fetchDevices()
  fetchPendingTokens()
})

// ── Register Device modal ──────────────────────────────────────────────────
const showModal = ref(false)
const step = ref<1 | 2>(1)
const generating = ref(false)
const shortCode = ref('')
const expiresAt = ref('')
const genError = ref('')
const qrDataUrl = ref('')
const qrSrvUrl = ref('')

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
    qrSrvUrl.value = useRuntimeConfig().public.supabase.url as string
    const qrPayload = JSON.stringify({ code: data.short_code, srv_url: qrSrvUrl.value })
    qrDataUrl.value = await QRCode.toDataURL(qrPayload, { width: 200, margin: 2 })
    step.value = 2
  } catch (err: unknown) {
    genError.value = err instanceof Error ? err.message : 'Failed to generate code'
  } finally {
    generating.value = false
  }
}

function closeModal() {
  showModal.value = false
  if (step.value === 2) {
    fetchDevices()
    fetchPendingTokens()
  }
}

async function showTokenQr(token: { short_code: string; expires_at: string }) {
  shortCode.value = token.short_code
  expiresAt.value = new Date(token.expires_at).toLocaleTimeString()
  qrSrvUrl.value = useRuntimeConfig().public.supabase.url as string
  const qrPayload = JSON.stringify({ code: token.short_code, srv_url: qrSrvUrl.value })
  qrDataUrl.value = await QRCode.toDataURL(qrPayload, { width: 200, margin: 2 })
  step.value = 2
  showModal.value = true
}

// ── Pending token helpers ────────────────────────────────────────────────────
function isExpired(expiresAt: string) {
  return new Date(expiresAt).getTime() < Date.now()
}

function expiresIn(expiresAt: string): string {
  const diff = new Date(expiresAt).getTime() - Date.now()
  if (diff <= 0) return 'Expired'
  const minutes = Math.floor(diff / 60000)
  if (minutes < 60) return `${minutes}m left`
  const hours = Math.floor(minutes / 60)
  return `${hours}h ${minutes % 60}m left`
}

const deletingTokenId = ref<string | null>(null)

async function handleDeleteToken(id: string) {
  deletingTokenId.value = id
  try {
    await deletePendingToken(id)
  } finally {
    deletingTokenId.value = null
  }
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

        <!-- Pending provisioning tokens -->
        <div v-if="pendingTokens.length > 0" class="space-y-2">
          <h2 class="text-sm font-medium text-muted-foreground">Pending Device Claims</h2>
          <div class="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-3">
            <div
              v-for="token in pendingTokens"
              :key="token.id"
              class="flex items-center justify-between rounded-lg border border-dashed p-3 cursor-pointer transition-colors hover:bg-muted/30"
              :class="isExpired(token.expires_at) ? 'border-muted opacity-60' : 'border-primary/30'"
              @click="!isExpired(token.expires_at) && showTokenQr(token)"
            >
              <div class="flex items-center gap-3 min-w-0">
                <span
                  class="font-mono text-sm font-semibold tracking-wider"
                  :class="isExpired(token.expires_at) ? 'text-muted-foreground line-through' : 'text-primary'"
                >
                  {{ token.short_code }}
                </span>
                <div class="min-w-0">
                  <p v-if="token.name" class="text-sm truncate">{{ token.name }}</p>
                  <p class="text-xs text-muted-foreground">{{ expiresIn(token.expires_at) }}</p>
                </div>
              </div>
              <button
                class="shrink-0 ml-2 inline-flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-destructive/10 hover:text-destructive"
                :disabled="deletingTokenId === token.id"
                @click.stop="handleDeleteToken(token.id)"
                title="Revoke token"
              >
                <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>
              </button>
            </div>
          </div>
        </div>

        <div v-if="loading" class="text-muted-foreground">Loading devices...</div>

        <div v-else-if="devices.length === 0" class="text-muted-foreground">
          No embedded devices registered yet.
        </div>

        <!-- ── Mobile: Card Layout (< lg) ── -->
        <div v-else class="flex flex-col gap-3 lg:hidden">
          <div
            v-for="device in devices"
            :key="device.id"
            class="rounded-lg border bg-card p-4 transition-colors"
          >
            <!-- Top row: Subdomain + Status + Delete -->
            <div class="flex items-center justify-between mb-3">
              <div class="flex items-center gap-2">
                <span class="font-mono text-base font-semibold">{{ device.subdomain }}</span>
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
              </div>
              <button
                class="inline-flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-destructive/10 hover:text-destructive"
                @click.prevent="openDeleteModal(device)"
                title="Delete device"
              >
                <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/></svg>
              </button>
            </div>

            <!-- Info grid -->
            <div class="grid grid-cols-2 gap-x-4 gap-y-2 text-sm">
              <div>
                <p class="text-xs text-muted-foreground">MAC Address</p>
                <p class="font-mono text-xs">{{ device.mac_address ?? '—' }}</p>
              </div>
              <div>
                <p class="text-xs text-muted-foreground">Last Seen</p>
                <p class="text-xs">{{ timeAgo(device.status_at) }}</p>
              </div>
              <div>
                <p class="text-xs text-muted-foreground">Machine</p>
                <NuxtLink
                  v-if="device.machine_id"
                  :to="`/machines/${device.machine_id}`"
                  class="text-xs text-primary hover:underline"
                >
                  {{ device.machine_name }}
                </NuxtLink>
                <p v-else class="text-xs text-muted-foreground">Unassigned</p>
              </div>
              <div>
                <p class="text-xs text-muted-foreground">Registered</p>
                <p class="text-xs">{{ new Date(device.created_at).toLocaleDateString() }}</p>
              </div>
              <div v-if="device.firmware_version" class="col-span-2">
                <p class="text-xs text-muted-foreground">Firmware</p>
                <p class="font-mono text-xs">
                  {{ device.firmware_version }}
                  <span v-if="device.firmware_build_date" class="text-muted-foreground">
                    ({{ new Date(device.firmware_build_date).toLocaleDateString() }})
                  </span>
                </p>
              </div>
            </div>
          </div>
        </div>

        <!-- ── Desktop: Table Layout (>= lg) ── -->
        <div v-if="!loading && devices.length > 0" class="hidden lg:block rounded-md border">
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
    class="fixed inset-0 z-50 flex items-end sm:items-center justify-center bg-black/40"
    @click.self="showDeleteModal = false"
  >
    <div class="w-full max-w-sm rounded-t-xl sm:rounded-xl border bg-card p-6 shadow-lg">
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
    class="fixed inset-0 z-50 flex items-end sm:items-center justify-center bg-black/40"
    @click.self="closeModal"
  >
    <div class="w-full max-w-md rounded-t-xl sm:rounded-xl border bg-card p-5 sm:p-6 shadow-lg max-h-[90vh] overflow-y-auto">
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

        <!-- Code + QR display -->
        <div class="mb-5 rounded-lg border-2 border-dashed border-primary/40 bg-primary/5 py-4 text-center">
          <p class="font-mono text-3xl sm:text-4xl font-bold tracking-[0.2em] sm:tracking-[0.3em] text-primary">{{ shortCode }}</p>
          <img v-if="qrDataUrl" :src="qrDataUrl" alt="QR Code" class="mx-auto mt-3 w-40 h-40 sm:w-[200px] sm:h-[200px]" />
          <p class="mt-2 text-xs text-muted-foreground">Scan this QR code on the device setup page</p>
          <p class="mt-1 text-xs text-muted-foreground break-all px-3">Server URL: <strong class="text-foreground font-mono">{{ qrSrvUrl }}</strong></p>
        </div>

        <!-- Instructions -->
        <ol class="mb-5 space-y-2 text-sm text-muted-foreground">
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">1</span>
            <span>Connect your phone to the device's WiFi network: <strong class="text-foreground">VMflow</strong> (password: <strong class="text-foreground">12345678</strong>)</span>
          </li>
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">2</span>
            <span>Open <strong class="text-foreground">192.168.4.1</strong> in your browser</span>
          </li>
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">3</span>
            <span>Tap <strong class="text-foreground">Scan QR Code</strong> and scan the code above — or enter the code and server URL manually</span>
          </li>
          <li class="flex gap-2">
            <span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-medium text-primary-foreground">4</span>
            <span>Select your WiFi network and save — the device will register automatically</span>
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
