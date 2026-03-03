<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'

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

                  <!-- Packing list -->
                  <div v-if="machine.tray_summary && machine.tray_summary.length > 0" class="space-y-1">
                    <p class="text-xs font-medium text-muted-foreground uppercase tracking-wide">Pack for this machine</p>
                    <div class="flex flex-wrap gap-x-3 gap-y-0.5">
                      <span
                        v-for="item in machine.tray_summary"
                        :key="item.product_id ?? item.product_name"
                        class="text-sm"
                      >
                        {{ item.deficit }}&times; {{ item.product_name }}
                      </span>
                    </div>
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
