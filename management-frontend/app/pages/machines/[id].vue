<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import AppSidebar from '@/components/AppSidebar.vue'
import SiteHeader from '@/components/SiteHeader.vue'
import { SidebarInset, SidebarProvider } from '@/components/ui/sidebar'
import { VisArea, VisAxis, VisLine, VisXYContainer } from '@unovis/vue'

const route = useRoute()
const supabase = useSupabaseClient()

const machine = ref<any>(null)
const sales = ref<any[]>([])
const loading = ref(true)
const errorMsg = ref('')

onMounted(async () => {
  const id = route.params.id as string
  try {
    const { data: machineData, error: machineError } = await supabase
      .from('vendingMachine')
      .select('id, name, location_lat, location_lon, embeddeds(id, status, status_at, subdomain)')
      .eq('id', id)
      .single()

    if (machineError) throw machineError
    machine.value = machineData

    if (machineData.embeddeds?.id) {
      const thirtyDaysAgo = new Date(Date.now() - 30 * 24 * 60 * 60 * 1000).toISOString()
      const { data: salesData, error: salesError } = await supabase
        .from('sales')
        .select('created_at, item_price, item_number, channel')
        .eq('embedded_id', machineData.embeddeds.id)
        .gte('created_at', thirtyDaysAgo)
        .order('created_at', { ascending: false })

      if (salesError) throw salesError
      sales.value = salesData ?? []
    }
  } catch (err: unknown) {
    errorMsg.value = err instanceof Error ? err.message : 'Failed to load machine'
  } finally {
    loading.value = false
  }
})

// Aggregate sales per day for the chart
const salesChartData = computed(() => {
  const byDay: Record<string, number> = {}
  for (const sale of sales.value) {
    const day = new Date(sale.created_at).toISOString().slice(0, 10)
    byDay[day] = (byDay[day] ?? 0) + (sale.item_price ?? 0)
  }
  return Object.entries(byDay)
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([date, total]) => ({ date: new Date(date), total }))
})

type ChartPoint = { date: Date; total: number }

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
      <div class="flex flex-1 flex-col gap-6 p-4 md:p-6">
        <div v-if="loading" class="text-muted-foreground">Loading…</div>
        <div v-else-if="errorMsg" class="text-destructive">{{ errorMsg }}</div>
        <template v-else-if="machine">
          <!-- Machine info -->
          <div class="flex items-start justify-between">
            <div>
              <h1 class="text-2xl font-semibold">{{ machine.name ?? 'Unnamed machine' }}</h1>
              <p v-if="machine.location_lat && machine.location_lon" class="mt-1 text-sm text-muted-foreground">
                {{ machine.location_lat.toFixed(5) }}, {{ machine.location_lon.toFixed(5) }}
              </p>
            </div>
            <div v-if="machine.embeddeds" class="text-right">
              <span
                class="rounded-full px-3 py-1 text-xs font-medium"
                :class="machine.embeddeds.status === 'online'
                  ? 'bg-green-100 text-green-700'
                  : 'bg-muted text-muted-foreground'"
              >
                {{ machine.embeddeds.status }}
              </span>
              <p class="mt-1 text-xs text-muted-foreground">Since {{ formatDate(machine.embeddeds.status_at) }}</p>
            </div>
          </div>

          <!-- Sales chart -->
          <div v-if="salesChartData.length > 0" class="rounded-xl border bg-card p-6">
            <h2 class="mb-4 text-sm font-medium">Daily revenue (last 30 days)</h2>
            <VisXYContainer :data="salesChartData" class="h-48 w-full">
              <VisArea :x="(d: ChartPoint) => d.date" :y="(d: ChartPoint) => d.total" color="var(--primary)" :opacity="0.3" />
              <VisLine :x="(d: ChartPoint) => d.date" :y="(d: ChartPoint) => d.total" color="var(--primary)" :line-width="2" />
              <VisAxis
                type="x"
                :x="(d: ChartPoint) => d.date"
                :tick-format="(d: number) => new Date(d).toLocaleDateString('en-US', { month: 'short', day: 'numeric' })"
                :num-ticks="6"
                :tick-line="false"
                :domain-line="false"
                :grid-line="false"
              />
              <VisAxis type="y" :num-ticks="3" :tick-line="false" :domain-line="false" />
            </VisXYContainer>
          </div>

          <!-- Sales table -->
          <div>
            <h2 class="mb-3 text-lg font-medium">Sales history</h2>
            <div v-if="sales.length === 0" class="text-sm text-muted-foreground">No sales recorded in the last 30 days.</div>
            <div v-else class="rounded-md border">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b bg-muted/50 text-left">
                    <th class="px-4 py-3 font-medium">Date</th>
                    <th class="px-4 py-3 font-medium">Amount</th>
                    <th class="px-4 py-3 font-medium">Item #</th>
                    <th class="px-4 py-3 font-medium">Channel</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="sale in sales"
                    :key="sale.id"
                    class="border-b last:border-0 hover:bg-muted/30 transition-colors"
                  >
                    <td class="px-4 py-3 text-muted-foreground">{{ formatDate(sale.created_at) }}</td>
                    <td class="px-4 py-3 font-medium">{{ formatCurrency(sale.item_price) }}</td>
                    <td class="px-4 py-3 text-muted-foreground">{{ sale.item_number }}</td>
                    <td class="px-4 py-3 capitalize text-muted-foreground">{{ sale.channel }}</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </template>
      </div>
    </SidebarInset>
  </SidebarProvider>
</template>
