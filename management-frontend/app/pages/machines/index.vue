<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Badge } from '@/components/ui/badge'
import { Separator } from '@/components/ui/separator'

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
const deviceName = ref('')

function openModal() {
  step.value = 1
  shortCode.value = ''
  expiresAt.value = ''
  genError.value = ''
  deviceName.value = ''
  showModal.value = true
}

async function generateCode() {
  generating.value = true
  genError.value = ''
  try {
    const body = deviceName.value.trim() ? { name: deviceName.value.trim() } : undefined
    const { data, error } = await supabase.functions.invoke('create-provisioning-token', { body })
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

function formatCurrency(amount: number | null | undefined) {
  if (amount == null) return '—'
  return new Intl.NumberFormat('en-US', { style: 'currency', currency: 'EUR' }).format(amount)
}
</script>

<template>
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

        <div v-if="loading" class="text-muted-foreground">Loading machines...</div>

        <div v-else-if="machines.length === 0" class="text-muted-foreground">
          No vending machines registered yet.
        </div>

        <div v-else class="grid grid-cols-1 gap-4 md:grid-cols-2 xl:grid-cols-3">
          <NuxtLink
            v-for="machine in machines"
            :key="machine.id"
            :to="`/machines/${machine.id}`"
            class="block transition-shadow hover:shadow-md rounded-xl"
          >
            <Card class="h-full">
              <CardHeader class="flex flex-row items-center justify-between space-y-0 pb-2">
                <CardTitle class="text-base font-semibold truncate">
                  {{ machine.name ?? 'Unnamed Machine' }}
                </CardTitle>
                <Badge
                  v-if="machine.embeddeds"
                  :variant="machine.embeddeds.status === 'online' || machine.embeddeds.status?.startsWith('ota_') ? 'default' : 'secondary'"
                  class="shrink-0"
                >
                  <span
                    class="mr-1 inline-block h-2 w-2 rounded-full"
                    :class="{
                      'bg-green-400': machine.embeddeds.status === 'online' || machine.embeddeds.status === 'ota_success',
                      'bg-yellow-400': machine.embeddeds.status === 'ota_updating',
                      'bg-red-400': machine.embeddeds.status === 'ota_failed',
                      'bg-muted-foreground/50': !['online', 'ota_updating', 'ota_success', 'ota_failed'].includes(machine.embeddeds.status),
                    }"
                  />
                  {{ machine.embeddeds.status === 'ota_updating' ? 'updating' : machine.embeddeds.status === 'ota_success' ? 'updated' : machine.embeddeds.status === 'ota_failed' ? 'update failed' : machine.embeddeds.status }}
                </Badge>
                <Badge v-else variant="outline" class="shrink-0">
                  No device
                </Badge>
              </CardHeader>
              <CardContent class="space-y-3">
                <!-- Revenue stats -->
                <div class="grid grid-cols-3 gap-2 text-center">
                  <div>
                    <p class="text-xs text-muted-foreground">Today</p>
                    <p class="text-lg font-semibold">{{ formatCurrency(machine.today_revenue ?? 0) }}</p>
                  </div>
                  <div>
                    <p class="text-xs text-muted-foreground">Yesterday</p>
                    <p class="text-lg font-semibold">{{ formatCurrency(machine.yesterday_revenue ?? 0) }}</p>
                  </div>
                  <div>
                    <p class="text-xs text-muted-foreground">Sales Today</p>
                    <p class="text-lg font-semibold">{{ machine.today_sales_count ?? 0 }}</p>
                  </div>
                </div>

                <Separator />

                <!-- Temperature & Stock placeholders -->
                <div class="grid grid-cols-2 gap-2 text-center">
                  <div>
                    <p class="text-xs text-muted-foreground">Temperature</p>
                    <p class="text-lg font-semibold text-muted-foreground">-- °C</p>
                  </div>
                  <div>
                    <p class="text-xs text-muted-foreground">Stock</p>
                    <p class="text-lg font-semibold text-muted-foreground">--</p>
                  </div>
                </div>

                <Separator />

                <!-- Last sale & traffic -->
                <div class="flex items-center justify-between text-sm">
                  <span class="text-muted-foreground">
                    Last: {{ timeAgo(machine.last_sale_at) }}
                    <template v-if="machine.last_sale_amount != null">
                      · {{ formatCurrency(machine.last_sale_amount) }}
                    </template>
                  </span>
                  <span class="text-muted-foreground">
                    Traffic: {{ machine.paxcounter_count ?? '—' }}
                  </span>
                </div>
              </CardContent>
            </Card>
          </NuxtLink>
        </div>
  </div>

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
        <div class="mb-4">
          <label for="device-name" class="mb-1.5 block text-sm font-medium">Device Name</label>
          <input
            id="device-name"
            v-model="deviceName"
            type="text"
            placeholder="e.g. Break Room Machine"
            class="flex h-9 w-full rounded-md border bg-transparent px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
          />
          <p class="mt-1 text-xs text-muted-foreground">Optional — leave blank for a default name.</p>
        </div>
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
