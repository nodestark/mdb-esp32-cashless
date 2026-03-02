<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { timeAgo } from '@/lib/utils'
import { useActivityLog } from '@/composables/useActivityLog'
import { Badge } from '@/components/ui/badge'

const {
  logs,
  loading,
  hasMore,
  entityTypeFilter,
  dateFrom,
  dateTo,
  fetchLogs,
  fetchMore,
  subscribe,
  actionLabel,
  entityTypeVariant,
} = useActivityLog()

const ENTITY_TYPES = [
  { value: '', label: 'All events' },
  { value: 'sale', label: 'Sales' },
  { value: 'credit', label: 'Credit sends' },
  { value: 'stock', label: 'Stock changes' },
  { value: 'firmware', label: 'Firmware' },
  { value: 'device', label: 'Devices' },
]

// Reload when filters change
watch([entityTypeFilter, dateFrom, dateTo], () => fetchLogs())

let unsubscribe: (() => void) | null = null

onMounted(async () => {
  await fetchLogs()
  unsubscribe = subscribe()
})

onUnmounted(() => {
  unsubscribe?.()
})

function entityTypeLabel(type: string) {
  const found = ENTITY_TYPES.find(e => e.value === type)
  return found?.label ?? type
}

function metadataChips(entry: { action: string; metadata: Record<string, unknown> | null }): { label: string; value: string }[] {
  const m = entry.metadata
  if (!m) return []
  const chips: { label: string; value: string }[] = []

  if (entry.action === 'sale_recorded') {
    if (m.item_number != null) chips.push({ label: 'Item', value: `#${m.item_number}` })
    if (m.price != null) chips.push({ label: 'Price', value: `€${Number(m.price).toFixed(2)}` })
    if (m.channel) chips.push({ label: 'Channel', value: String(m.channel) })
    if (m.device_id) chips.push({ label: 'Device', value: String(m.device_id).slice(0, 8) + '…' })
  }

  if (entry.action === 'credit_sent') {
    if (m.amount != null) chips.push({ label: 'Amount', value: `€${Number(m.amount).toFixed(2)}` })
    if (m.device_id) chips.push({ label: 'Device', value: String(m.device_id).slice(0, 8) + '…' })
  }

  if (entry.action === 'stock_updated') {
    if (m.machine_name) chips.push({ label: 'Machine', value: String(m.machine_name) })
    if (m.product_name) chips.push({ label: 'Product', value: String(m.product_name) })
    if (m.item_number != null) chips.push({ label: 'Slot', value: `#${m.item_number}` })
    // Stock change
    if (m.old_stock != null && m.new_stock != null) {
      chips.push({ label: 'Stock', value: `${m.old_stock} → ${m.new_stock}` })
    } else if (m.new_stock != null) {
      chips.push({ label: 'Stock', value: String(m.new_stock) })
    }
    // Min stock change
    if (m.old_min_stock != null && m.new_min_stock != null) {
      chips.push({ label: 'Min stock', value: `${m.old_min_stock} → ${m.new_min_stock}` })
    } else if (m.new_min_stock != null) {
      chips.push({ label: 'Min stock', value: String(m.new_min_stock) })
    }
    // Capacity change
    if (m.old_capacity != null && m.new_capacity != null) {
      chips.push({ label: 'Capacity', value: `${m.old_capacity} → ${m.new_capacity}` })
    } else if (m.new_capacity != null) {
      chips.push({ label: 'Capacity', value: String(m.new_capacity) })
    }
  }

  if (entry.action === 'stock_refill_all') {
    if (m.machine_name) chips.push({ label: 'Machine', value: String(m.machine_name) })
    const trays = m.trays_refilled as any[]
    if (trays?.length) {
      chips.push({ label: 'Trays', value: `${trays.length} refilled` })
      // Show each tray inline
      for (const t of trays) {
        const name = t.product_name ? `${t.product_name}` : `Slot #${t.item_number}`
        chips.push({ label: name, value: `${t.old_stock} → ${t.new_stock}` })
      }
    }
  }

  return chips
}
</script>

<template>
  <div class="flex flex-1 flex-col gap-6 overflow-x-hidden p-4 md:p-6">
    <!-- Header -->
    <div class="flex flex-wrap items-center justify-between gap-4">
      <div>
        <h1 class="text-2xl font-bold tracking-tight">History</h1>
        <p class="text-sm text-muted-foreground">Platform activity log — updated in real time</p>
      </div>
    </div>

    <!-- Filters -->
    <div class="flex flex-wrap gap-3">
      <select
        v-model="entityTypeFilter"
        class="h-9 rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
      >
        <option v-for="opt in ENTITY_TYPES" :key="opt.value" :value="opt.value">
          {{ opt.label }}
        </option>
      </select>

      <input
        v-model="dateFrom"
        type="date"
        placeholder="From"
        class="h-9 rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
      />
      <input
        v-model="dateTo"
        type="date"
        placeholder="To"
        class="h-9 rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
      />

      <button
        v-if="entityTypeFilter || dateFrom || dateTo"
        class="h-9 rounded-md border border-input px-3 py-1 text-sm text-muted-foreground hover:bg-muted"
        @click="entityTypeFilter = ''; dateFrom = ''; dateTo = ''"
      >
        Clear filters
      </button>
    </div>

    <!-- Loading skeleton -->
    <div v-if="loading && logs.length === 0" class="space-y-2">
      <div
        v-for="i in 8"
        :key="i"
        class="h-14 animate-pulse rounded-lg bg-muted"
      />
    </div>

    <!-- Empty state -->
    <div
      v-else-if="!loading && logs.length === 0"
      class="flex flex-col items-center justify-center gap-2 py-24 text-center text-muted-foreground"
    >
      <span class="text-4xl">📋</span>
      <p class="font-medium">No activity yet</p>
      <p class="text-sm">Events will appear here as they happen.</p>
    </div>

    <!-- Log table -->
    <div v-else class="overflow-hidden rounded-lg border">
      <table class="w-full text-sm">
        <thead>
          <tr class="border-b bg-muted/50">
            <th class="px-4 py-3 text-left font-medium text-muted-foreground">Time</th>
            <th class="px-4 py-3 text-left font-medium text-muted-foreground">Type</th>
            <th class="px-4 py-3 text-left font-medium text-muted-foreground">Action</th>
            <th class="px-4 py-3 text-left font-medium text-muted-foreground">Details</th>
            <th class="px-4 py-3 text-left font-medium text-muted-foreground">User</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="entry in logs"
            :key="entry.id"
            class="border-b transition-colors last:border-0 hover:bg-muted/30"
          >
            <td class="whitespace-nowrap px-4 py-3 text-muted-foreground">
              <span :title="new Date(entry.created_at).toLocaleString()">
                {{ timeAgo(entry.created_at) }}
              </span>
            </td>
            <td class="px-4 py-3">
              <Badge :variant="entityTypeVariant(entry.entity_type)" class="capitalize">
                {{ entry.entity_type }}
              </Badge>
            </td>
            <td class="px-4 py-3 font-medium">
              {{ actionLabel(entry.action) }}
            </td>
            <td class="px-4 py-3">
              <div class="flex flex-wrap gap-1.5">
                <span
                  v-for="chip in metadataChips(entry)"
                  :key="chip.label"
                  class="inline-flex items-center gap-1 rounded-full border px-2 py-0.5 text-xs"
                >
                  <span class="text-muted-foreground">{{ chip.label }}</span>
                  <span class="font-medium">{{ chip.value }}</span>
                </span>
                <span v-if="metadataChips(entry).length === 0" class="text-muted-foreground">—</span>
              </div>
            </td>
            <td class="px-4 py-3">
              <span
                :class="entry.user_id ? 'text-foreground' : 'italic text-muted-foreground'"
                class="text-sm"
              >
                {{ entry.user_display }}
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Load more -->
    <div v-if="hasMore && logs.length > 0" class="flex justify-center">
      <button
        :disabled="loading"
        class="rounded-md border border-input px-4 py-2 text-sm hover:bg-muted disabled:opacity-50"
        @click="fetchMore"
      >
        {{ loading ? 'Loading…' : 'Load more' }}
      </button>
    </div>
  </div>
</template>
