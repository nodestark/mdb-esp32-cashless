<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Separator } from '@/components/ui/separator'
import { getProductImageUrl } from '@/composables/useProducts'
import { formatCurrency } from '@/lib/utils'

const { organization } = useOrganization()
const {
  machines, loading, fetchMachines, subscribeToStatusUpdates, createMachine,
} = useMachines()
const { onResume } = useAppResume()

// Re-fetch all machine data when app resumes from background (iOS PWA etc.)
onResume(() => fetchMachines())

onMounted(async () => {
  await fetchMachines()
  const unsubscribe = subscribeToStatusUpdates()
  onUnmounted(unsubscribe)
})

// ── Add Machine modal ────────────────────────────────────────────────────────
const showMachineModal = ref(false)
const machineName = ref('')
const machineError = ref('')
const creatingMachine = ref(false)

function openMachineModal() {
  machineName.value = ''
  machineError.value = ''
  showMachineModal.value = true
}

async function submitCreateMachine() {
  if (!machineName.value.trim()) {
    machineError.value = 'Name is required'
    return
  }
  creatingMachine.value = true
  machineError.value = ''
  try {
    await createMachine(machineName.value.trim(), organization.value!.id)
    showMachineModal.value = false
  } catch (err: unknown) {
    machineError.value = err instanceof Error ? err.message : 'Failed to create machine'
  } finally {
    creatingMachine.value = false
  }
}

// ── Packing checklist state (local only, resets on page reload) ──────────────
// Map<machineId, Set<productKey>>
const packedItems = ref(new Map<string, Set<string>>())

function itemKey(item: { product_id: string | null; product_name: string }) {
  return item.product_id ?? item.product_name
}

function isPacked(machineId: string, item: { product_id: string | null; product_name: string }) {
  return packedItems.value.get(machineId)?.has(itemKey(item)) ?? false
}

function togglePacked(machineId: string, item: { product_id: string | null; product_name: string }) {
  const key = itemKey(item)
  const current = packedItems.value.get(machineId) ?? new Set<string>()
  if (current.has(key)) {
    current.delete(key)
  } else {
    current.add(key)
  }
  packedItems.value.set(machineId, current)
  // Trigger reactivity
  packedItems.value = new Map(packedItems.value)
}

function allPacked(machineId: string, traySummary: { product_id: string | null; product_name: string }[]) {
  if (!traySummary || traySummary.length === 0) return false
  const set = packedItems.value.get(machineId)
  if (!set) return false
  return traySummary.every(item => set.has(itemKey(item)))
}
</script>

<template>
  <div class="flex flex-1 flex-col gap-4 p-4 md:p-6">
        <div class="flex items-center justify-between">
          <h1 class="text-2xl font-semibold">Vending Machines</h1>
          <button
            class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
            @click="openMachineModal"
          >
            Add Machine
          </button>
        </div>

        <div v-if="loading" class="text-muted-foreground">Loading machines...</div>

        <div v-else-if="machines.length === 0" class="text-muted-foreground">
          No vending machines registered yet.
        </div>

        <div v-else class="grid grid-cols-1 gap-4 md:grid-cols-2 xl:grid-cols-3">
          <div
            v-for="machine in machines"
            :key="machine.id"
            class="block rounded-xl transition-shadow hover:shadow-md"
          >
            <Card class="h-full">
              <CardHeader class="flex flex-row items-center justify-between space-y-0 pb-2">
                <NuxtLink :to="`/machines/${machine.id}`" class="min-w-0 flex-1">
                  <CardTitle class="text-base font-semibold truncate">
                    {{ machine.name ?? 'Unnamed Machine' }}
                  </CardTitle>
                </NuxtLink>
                <!-- Stock health dot -->
                <span
                  class="ml-2 inline-block h-3 w-3 shrink-0 rounded-full"
                  :class="{
                    'bg-red-500': machine.stock_health === 'critical',
                    'bg-amber-500': machine.stock_health === 'low',
                    'bg-green-500': machine.stock_health === 'ok' || !machine.stock_health,
                  }"
                />
              </CardHeader>

              <CardContent class="space-y-3">
                <!-- Sales analytics -->
                <div class="grid grid-cols-2 gap-x-4 gap-y-1 text-xs">
                  <div class="flex justify-between">
                    <span class="text-muted-foreground">Today</span>
                    <span class="font-medium tabular-nums">{{ formatCurrency(machine.today_revenue ?? 0) }} <span class="text-muted-foreground font-normal">({{ machine.today_sales_count ?? 0 }})</span></span>
                  </div>
                  <div class="flex justify-between">
                    <span class="text-muted-foreground">This month</span>
                    <span class="font-medium tabular-nums">{{ formatCurrency(machine.this_month_revenue ?? 0) }} <span class="text-muted-foreground font-normal">({{ machine.this_month_sales_count ?? 0 }})</span></span>
                  </div>
                  <div class="flex justify-between">
                    <span class="text-muted-foreground">Yesterday</span>
                    <span class="font-medium tabular-nums">{{ formatCurrency(machine.yesterday_revenue ?? 0) }} <span class="text-muted-foreground font-normal">({{ machine.yesterday_sales_count ?? 0 }})</span></span>
                  </div>
                  <div class="flex justify-between">
                    <span class="text-muted-foreground">Last month</span>
                    <span class="font-medium tabular-nums">{{ formatCurrency(machine.last_month_revenue ?? 0) }} <span class="text-muted-foreground font-normal">({{ machine.last_month_sales_count ?? 0 }})</span></span>
                  </div>
                </div>

                <Separator />

                <!-- Healthy machine: compact view -->
                <template v-if="machine.stock_health === 'ok' || !machine.stock_health">
                  <p class="text-sm text-muted-foreground">
                    <template v-if="(machine.total_trays ?? 0) > 0">
                      All stocked ({{ machine.total_trays }} trays)
                    </template>
                    <template v-else>
                      No trays configured
                    </template>
                  </p>
                </template>

                <!-- Machine needing refill: expanded view -->
                <template v-else>
                  <!-- Urgency summary -->
                  <p class="text-sm">
                    <span v-if="(machine.empty_trays ?? 0) > 0" class="font-medium text-red-500">{{ machine.empty_trays }} empty</span>
                    <span v-if="(machine.empty_trays ?? 0) > 0 && ((machine.low_trays ?? 0) - (machine.empty_trays ?? 0)) > 0"> &middot; </span>
                    <span v-if="((machine.low_trays ?? 0) - (machine.empty_trays ?? 0)) > 0" class="font-medium text-amber-500">{{ (machine.low_trays ?? 0) - (machine.empty_trays ?? 0) }} low</span>
                    <span class="text-muted-foreground"> of {{ machine.total_trays }} trays</span>
                  </p>

                  <!-- Stock bar -->
                  <div class="flex items-center gap-2">
                    <div class="h-2 flex-1 overflow-hidden rounded-full bg-muted">
                      <div
                        class="h-full rounded-full transition-all"
                        :class="{
                          'bg-red-500': (machine.stock_percent ?? 0) < 20,
                          'bg-amber-500': (machine.stock_percent ?? 0) >= 20 && (machine.stock_percent ?? 0) < 50,
                          'bg-green-500': (machine.stock_percent ?? 0) >= 50,
                        }"
                        :style="{ width: `${machine.stock_percent ?? 0}%` }"
                      />
                    </div>
                    <span class="text-xs font-medium text-muted-foreground w-8 text-right">{{ machine.stock_percent ?? 0 }}%</span>
                  </div>

                  <!-- Packing checklist -->
                  <div v-if="machine.tray_summary && machine.tray_summary.length > 0" class="space-y-2">
                    <div class="flex items-center justify-between">
                      <p class="text-xs font-medium text-muted-foreground uppercase tracking-wide">Pack for this machine</p>
                      <span
                        v-if="allPacked(machine.id, machine.tray_summary)"
                        class="text-xs font-medium text-green-600"
                      >
                        All packed
                      </span>
                    </div>
                    <ul class="space-y-1">
                      <li
                        v-for="item in machine.tray_summary"
                        :key="item.product_id ?? item.product_name"
                        class="flex items-center gap-2 rounded-md px-2 py-1.5 -mx-2 cursor-pointer select-none transition-colors hover:bg-muted/50 active:bg-muted"
                        @click="togglePacked(machine.id, item)"
                      >
                        <span
                          class="flex h-5 w-5 shrink-0 items-center justify-center rounded border transition-colors"
                          :class="isPacked(machine.id, item)
                            ? 'bg-primary border-primary text-primary-foreground'
                            : 'border-muted-foreground/30'"
                        >
                          <svg v-if="isPacked(machine.id, item)" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" class="h-3 w-3"><polyline points="20 6 9 17 4 12" /></svg>
                        </span>
                        <img
                          v-if="item.image_path"
                          :src="getProductImageUrl(item.image_path)"
                          :alt="item.product_name"
                          class="h-8 w-8 shrink-0 rounded object-cover transition-opacity"
                          :class="isPacked(machine.id, item) ? 'opacity-40' : ''"
                        />
                        <span
                          v-else
                          class="flex h-8 w-8 shrink-0 items-center justify-center rounded bg-muted text-xs text-muted-foreground transition-opacity"
                          :class="isPacked(machine.id, item) ? 'opacity-40' : ''"
                        >
                          <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" class="h-4 w-4"><rect width="18" height="18" x="3" y="3" rx="2" /><circle cx="9" cy="9" r="2" /><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21" /></svg>
                        </span>
                        <span
                          class="text-sm transition-all"
                          :class="isPacked(machine.id, item) ? 'line-through text-muted-foreground/50' : ''"
                        >
                          {{ item.deficit }}&times; {{ item.product_name }}
                        </span>
                      </li>
                    </ul>
                  </div>

                  <!-- Refill button -->
                  <NuxtLink
                    :to="`/machines/${machine.id}?tab=stock`"
                    class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
                  >
                    Refill &rarr;
                  </NuxtLink>
                </template>
              </CardContent>
            </Card>
          </div>
        </div>
  </div>

  <!-- Add Machine Modal -->
  <div
    v-if="showMachineModal"
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    @click.self="showMachineModal = false"
  >
    <div class="w-full max-w-md rounded-xl border bg-card p-6 shadow-lg">
      <h2 class="mb-1 text-lg font-semibold">Add Machine</h2>
      <p class="mb-5 text-sm text-muted-foreground">
        Create a vending machine. You can assign a device to it later.
      </p>
      <div class="mb-4">
        <label for="machine-name" class="mb-1.5 block text-sm font-medium">Machine Name</label>
        <input
          id="machine-name"
          v-model="machineName"
          type="text"
          placeholder="e.g. Break Room Machine"
          class="flex h-9 w-full rounded-md border bg-transparent px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
          @keydown.enter="submitCreateMachine"
        />
      </div>
      <p v-if="machineError" class="mb-3 text-sm text-destructive">{{ machineError }}</p>
      <div class="flex gap-2">
        <button
          class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium hover:bg-muted"
          @click="showMachineModal = false"
        >
          Cancel
        </button>
        <button
          :disabled="creatingMachine"
          class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow hover:bg-primary/90 disabled:opacity-50"
          @click="submitCreateMachine"
        >
          <span v-if="creatingMachine">Creating...</span>
          <span v-else>Create</span>
        </button>
      </div>
    </div>
  </div>

</template>
