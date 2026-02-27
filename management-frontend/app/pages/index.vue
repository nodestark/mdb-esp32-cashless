<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import AppSidebar from "@/components/AppSidebar.vue"
import ChartAreaInteractive from "@/components/ChartAreaInteractive.vue"
import SectionCards from "@/components/SectionCards.vue"
import SiteHeader from "@/components/SiteHeader.vue"
import {
  SidebarInset,
  SidebarProvider,
} from "@/components/ui/sidebar"

const supabase = useSupabaseClient()
const { fetchOrganization } = useOrganization()

// KPI state
const todaySales = ref(0)
const weekSales = ref(0)
const machinesOnline = ref(0)
const totalMachines = ref(0)
const salesChartData = ref<{ date: Date; total: number }[]>([])

onMounted(async () => {
  await fetchOrganization()
  await loadDashboard()
})

async function loadDashboard() {
  const now = new Date()
  const todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).toISOString()
  const weekStart = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000).toISOString()
  const thirtyDaysAgo = new Date(now.getTime() - 30 * 24 * 60 * 60 * 1000).toISOString()

  const [
    todaySalesRes,
    weekSalesRes,
    machinesRes,
    salesHistoryRes,
  ] = await Promise.all([
    supabase.from('sales').select('item_price').gte('created_at', todayStart),
    supabase.from('sales').select('item_price').gte('created_at', weekStart),
    supabase.from('vendingMachine').select('id, embeddeds(status)'),
    supabase.from('sales').select('created_at, item_price').gte('created_at', thirtyDaysAgo),
  ])

  // Today's total
  todaySales.value = (todaySalesRes.data ?? []).reduce((sum: number, s: any) => sum + (s.item_price ?? 0), 0)

  // Week total
  weekSales.value = (weekSalesRes.data ?? []).reduce((sum: number, s: any) => sum + (s.item_price ?? 0), 0)

  // Machine counts
  const machines = machinesRes.data ?? []
  totalMachines.value = machines.length
  machinesOnline.value = machines.filter((m: any) => m.embeddeds?.status === 'online').length

  // Aggregate sales by day
  const byDay: Record<string, number> = {}
  for (const sale of salesHistoryRes.data ?? []) {
    const day = new Date(sale.created_at).toISOString().slice(0, 10)
    byDay[day] = (byDay[day] ?? 0) + (sale.item_price ?? 0)
  }
  salesChartData.value = Object.entries(byDay)
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([date, total]) => ({ date: new Date(date), total }))
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
      <div class="flex flex-1 flex-col">
        <div class="@container/main flex flex-1 flex-col gap-2">
          <div class="flex flex-col gap-4 py-4 md:gap-6 md:py-6">
            <SectionCards
              :today-sales="todaySales"
              :week-sales="weekSales"
              :machines-online="machinesOnline"
              :total-machines="totalMachines"
            />
            <div class="px-4 lg:px-6">
              <ChartAreaInteractive :data="salesChartData" />
            </div>
          </div>
        </div>
      </div>
    </SidebarInset>
  </SidebarProvider>
</template>
