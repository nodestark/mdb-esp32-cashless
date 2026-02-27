<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import AppSidebar from '@/components/AppSidebar.vue'
import SiteHeader from '@/components/SiteHeader.vue'
import { SidebarInset, SidebarProvider } from '@/components/ui/sidebar'

const { machines, loading, fetchMachines, subscribeToStatusUpdates } = useMachines()

onMounted(async () => {
  await fetchMachines()
  const unsubscribe = subscribeToStatusUpdates()
  onUnmounted(unsubscribe)
})

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
</template>
