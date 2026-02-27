<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import AppSidebar from '@/components/AppSidebar.vue'
import SiteHeader from '@/components/SiteHeader.vue'
import { SidebarInset, SidebarProvider } from '@/components/ui/sidebar'

const { machines, loading, fetchMachines, subscribeToStatusUpdates } = useMachines()
const supabase = useSupabaseClient()

onMounted(async () => {
  await fetchMachines()
  const unsubscribe = subscribeToStatusUpdates()
  onUnmounted(unsubscribe)
})

// ── Add Device modal ──────────────────────────────────────────────────────────
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
    const { data, error } = await supabase.functions.invoke('create-provisioning-token')
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
  if (step.value === 2) fetchMachines()
}

// ── Helpers ───────────────────────────────────────────────────────────────────
function statusClass(status: string) {
  return status === 'online' ? 'text-green-600' : 'text-muted-foreground'
}
function formatDate(dt: string | null | undefined) {
  if (!dt) return '—'
  return new Date(dt).toLocaleString()
}
function formatCurrency(amount: number | null | undefined) {
  if (amount == null) return '—'
  return new Intl.NumberFormat('en-US', { style: 'currency', currency: 'EUR' }).format(amount)
}
</script>

<template>
  <SidebarProvider
    :style="{
      '--sidebar-width': 'calc(var(--spacing) * 72)',
      '--header-height': 'calc(var(--spacing) * 12)',
    }"
  >
    <AppSidebar variant="inset" />
    <SidebarInset>
      <SiteHeader />
      <div class="flex flex-1 flex-col gap-4 p-4 md:p-6">
        <div class="flex items-center justify-between">
          <h1 class="text-2xl font-semibold">Vending Machines</h1>
          <button
            class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
            @click="openModal"
          >
            Add Device
          </button>
        </div>

        <div v-if="loading" class="text-muted-foreground">Loading machines…</div>

        <div v-else-if="machines.length === 0" class="text-muted-foreground">
          No vending machines registered yet.
        </div>

        <div v-else class="rounded-md border">
          <table class="w-full text-sm">
            <thead>
              <tr class="border-b bg-muted/50 text-left">
                <th class="px-4 py-3 font-medium">Name</th>
                <th class="px-4 py-3 font-medium">Location</th>
                <th class="px-4 py-3 font-medium">Status</th>
                <th class="px-4 py-3 font-medium">Last Sale</th>
                <th class="px-4 py-3 font-medium">Last Sale Amount</th>
                <th class="px-4 py-3 font-medium"></th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="machine in machines"
                :key="machine.id"
                class="border-b last:border-0 hover:bg-muted/30 transition-colors"
              >
                <td class="px-4 py-3 font-medium">{{ machine.name ?? '—' }}</td>
                <td class="px-4 py-3 text-muted-foreground">
                  <span v-if="machine.location_lat && machine.location_lon">
                    {{ machine.location_lat.toFixed(4) }}, {{ machine.location_lon.toFixed(4) }}
                  </span>
                  <span v-else>—</span>
                </td>
                <td class="px-4 py-3">
                  <span v-if="machine.embeddeds" :class="statusClass(machine.embeddeds.status)" class="font-medium capitalize">
                    {{ machine.embeddeds.status }}
                  </span>
                  <span v-else class="text-muted-foreground">No device</span>
                </td>
                <td class="px-4 py-3 text-muted-foreground">{{ formatDate(machine.last_sale_at) }}</td>
                <td class="px-4 py-3">{{ formatCurrency(machine.last_sale_amount) }}</td>
                <td class="px-4 py-3">
                  <NuxtLink
                    :to="`/machines/${machine.id}`"
                    class="text-primary text-xs underline-offset-4 hover:underline"
                  >
                    Details
                  </NuxtLink>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </SidebarInset>
  </SidebarProvider>

  <!-- Add Device Modal -->
  <div
    v-if="showModal"
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    @click.self="closeModal"
  >
    <div class="w-full max-w-md rounded-xl border bg-card p-6 shadow-lg">

      <!-- Step 1: Generate code -->
      <template v-if="step === 1">
        <h2 class="mb-1 text-lg font-semibold">Add a Device</h2>
        <p class="mb-5 text-sm text-muted-foreground">
          Generate a one-time provisioning code. You'll enter it into the device's WiFi setup page.
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
            <span v-if="generating">Generating…</span>
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
