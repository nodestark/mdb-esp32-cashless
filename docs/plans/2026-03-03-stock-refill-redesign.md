# Stock-First Machines Page Redesign — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Redesign `/machines` as a stock-urgency dashboard where operators instantly see which machines need refilling and what to pack, with a tray-by-tray refill flow on the detail page.

**Architecture:** Extend `useMachines` composable to batch-fetch `machine_trays` data and aggregate stock stats per machine. Redesign the `/machines` page cards from revenue-focused to stock-urgency-focused. On the detail page, support `?tab=stock` query param to jump directly to the Trays & Stock tab with low-tray visual emphasis.

**Tech Stack:** Nuxt 4 (Vue 3 Composition API), TypeScript, Supabase (realtime subscriptions), shadcn-nuxt (Card, Badge), TailwindCSS 4.

**Design doc:** `docs/plans/2026-03-03-stock-refill-redesign-design.md`

---

### Task 1: Extend VendingMachine Interface with Stock Fields

**Files:**
- Modify: `management-frontend/app/composables/useMachines.ts:12-27`

**Step 1: Add stock fields to the VendingMachine interface**

In `management-frontend/app/composables/useMachines.ts`, add these fields to the `VendingMachine` interface (after the existing `paxcounter_count` field at line 26):

```typescript
interface VendingMachine {
  id: string
  name: string
  location_lat: number | null
  location_lon: number | null
  embedded: string | null
  embeddeds: Embedded | null
  last_sale_at?: string | null
  last_sale_amount?: number | null
  last_sale_item_number?: number | null
  today_revenue?: number
  today_sales_count?: number
  yesterday_revenue?: number
  paxcounter_count?: number | null
  // Stock fields (new)
  total_trays?: number
  low_trays?: number
  empty_trays?: number
  stock_health?: 'ok' | 'low' | 'critical'
  stock_percent?: number
  tray_summary?: { product_name: string; product_id: string | null; deficit: number }[]
}
```

**Step 2: Verify the app still compiles**

Run: `cd management-frontend && npx nuxi typecheck`
Expected: No new type errors (existing `as` casts remain compatible).

**Step 3: Commit**

```bash
git add management-frontend/app/composables/useMachines.ts
git commit -m "feat: add stock fields to VendingMachine interface"
```

---

### Task 2: Add Stock Aggregation Query to fetchMachines()

**Files:**
- Modify: `management-frontend/app/composables/useMachines.ts:42-150` (the `fetchMachines` function)

**Step 1: Add machine_trays batch query**

Inside `fetchMachines()`, after the existing `Promise.all` block (which fetches `todaySalesRes`, `yesterdaySalesRes`, `paxRes`, `lastSaleResults`), add a second query to fetch all tray data for the company's machines in one go. Insert this after line 99 (closing of the existing `Promise.all`) and before the aggregation section:

```typescript
      // Fetch all tray data in one batch (with product names)
      const { data: allTrays } = await supabase
        .from('machine_trays')
        .select('machine_id, item_number, product_id, capacity, current_stock, min_stock, products(name)')
        .in('machine_id', machineIds)
```

Then, after the existing aggregation loops (after applying paxcounter data at line ~146), add stock aggregation:

```typescript
      // Aggregate stock stats per machine
      const trayRows = (allTrays ?? []) as {
        machine_id: string
        item_number: number
        product_id: string | null
        capacity: number
        current_stock: number
        min_stock: number
        products: { name: string } | null
      }[]

      const stockMap = new Map<string, {
        total: number
        low: number
        empty: number
        totalStock: number
        totalCapacity: number
        deficits: Map<string, { product_name: string; product_id: string | null; deficit: number }>
      }>()

      for (const tray of trayRows) {
        if (!tray.machine_id) continue
        let entry = stockMap.get(tray.machine_id)
        if (!entry) {
          entry = { total: 0, low: 0, empty: 0, totalStock: 0, totalCapacity: 0, deficits: new Map() }
          stockMap.set(tray.machine_id, entry)
        }
        entry.total++
        entry.totalStock += tray.current_stock
        entry.totalCapacity += tray.capacity

        const isLow = tray.min_stock > 0 && tray.current_stock <= tray.min_stock
        const isEmpty = tray.current_stock === 0

        if (isEmpty) entry.empty++
        else if (isLow) entry.low++

        if (isLow || isEmpty) {
          const deficit = tray.capacity - tray.current_stock
          const productName = tray.products?.name ?? `Slot ${tray.item_number}`
          const key = tray.product_id ?? `slot-${tray.item_number}`
          const existing = entry.deficits.get(key)
          if (existing) {
            existing.deficit += deficit
          } else {
            entry.deficits.set(key, { product_name: productName, product_id: tray.product_id, deficit })
          }
        }
      }

      // Apply stock stats to machines
      for (const machine of machines.value) {
        const stock = stockMap.get(machine.id)
        if (stock) {
          machine.total_trays = stock.total
          machine.low_trays = stock.low + stock.empty
          machine.empty_trays = stock.empty
          machine.stock_health = stock.empty > 0 ? 'critical' : (stock.low > 0 ? 'low' : 'ok')
          machine.stock_percent = stock.totalCapacity > 0
            ? Math.round((stock.totalStock / stock.totalCapacity) * 100)
            : 0
          machine.tray_summary = Array.from(stock.deficits.values()).sort((a, b) => b.deficit - a.deficit)
        } else {
          machine.total_trays = 0
          machine.low_trays = 0
          machine.empty_trays = 0
          machine.stock_health = 'ok'
          machine.stock_percent = 0
          machine.tray_summary = []
        }
      }
```

**Step 2: Add sort by urgency**

After the stock aggregation, add sorting. Place this right before the `} finally {` block:

```typescript
      // Sort machines by stock urgency: critical > low > ok, then by low_trays desc
      const healthOrder = { critical: 0, low: 1, ok: 2 }
      machines.value.sort((a, b) => {
        const ha = healthOrder[a.stock_health ?? 'ok']
        const hb = healthOrder[b.stock_health ?? 'ok']
        if (ha !== hb) return ha - hb
        return (b.low_trays ?? 0) - (a.low_trays ?? 0)
      })
```

**Step 3: Verify the app compiles and fetchMachines still works**

Run: `cd management-frontend && npx nuxi typecheck`
Expected: No type errors.

**Step 4: Commit**

```bash
git add management-frontend/app/composables/useMachines.ts
git commit -m "feat: add stock aggregation to fetchMachines"
```

---

### Task 3: Add machine_trays Realtime Subscription

**Files:**
- Modify: `management-frontend/app/composables/useMachines.ts:193-268` (the `subscribeToStatusUpdates` function)

**Step 1: Add machine_trays channel to subscribeToStatusUpdates**

In the `subscribeToStatusUpdates()` function, add a new `.on()` handler to the existing `channel` chain. Add this before the `.subscribe()` call (before line 264):

```typescript
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'machine_trays' },
        (payload) => {
          // A tray was inserted, updated, or deleted — re-fetch stock stats
          // We identify the machine from the payload and re-aggregate
          const row = (payload.new ?? payload.old) as Record<string, any>
          const machineId = row?.machine_id
          if (!machineId) return

          // Re-fetch all machine data to get updated stock aggregation
          // This is simple and correct — a targeted approach would be a premature optimization
          fetchMachines()
        }
      )
```

**Step 2: Commit**

```bash
git add management-frontend/app/composables/useMachines.ts
git commit -m "feat: subscribe to machine_trays changes for live stock updates"
```

---

### Task 4: Redesign /machines Page — Stock-Urgency Cards

**Files:**
- Modify: `management-frontend/app/pages/machines/index.vue` (full template rewrite of the card section)

**Step 1: Rewrite the machine card template**

Replace the entire `<template>` section of `management-frontend/app/pages/machines/index.vue`. The new template replaces the revenue-focused cards with stock-urgency cards. Keep the Add Machine modal unchanged.

Replace the card grid (the `<div v-else class="grid ...">` section, lines 74-153) with:

```html
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
                    <span v-if="(machine.empty_trays ?? 0) > 0 && ((machine.low_trays ?? 0) - (machine.empty_trays ?? 0)) > 0"> · </span>
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
                    Refill →
                  </NuxtLink>
                </template>
              </CardContent>
            </Card>
          </div>
        </div>
```

**Step 2: Remove unused imports**

In the `<script setup>` section, remove the `Separator` import (line 6) and `timeAgo, formatCurrency` imports (line 7) since the new cards no longer use them:

```typescript
// Remove these lines:
// import { Separator } from '@/components/ui/separator'
// import { timeAgo, formatCurrency } from '@/lib/utils'
```

Keep all other script imports and logic exactly as-is (the Add Machine modal code stays).

**Step 3: Verify the page loads correctly**

Run: `cd management-frontend && npm run dev`
Navigate to `/machines` and verify:
- Cards render with stock health dots
- Healthy machines show compact "All stocked" text
- Machines with low/empty trays show expanded view with stock bar and packing list
- "Refill" button links to `/machines/[id]?tab=stock`

**Step 4: Commit**

```bash
git add management-frontend/app/pages/machines/index.vue
git commit -m "feat: redesign /machines page with stock-urgency cards"
```

---

### Task 5: Support ?tab=stock Query Param on Detail Page

**Files:**
- Modify: `management-frontend/app/pages/machines/[id].vue:9,846`

**Step 1: Read query param and compute default tab**

In `management-frontend/app/pages/machines/[id].vue`, after `const route = useRoute()` (line 9), add:

```typescript
const defaultTab = computed(() => route.query.tab === 'stock' ? 'trays' : 'sales')
```

**Step 2: Use computed value in Tabs component**

At line 846, change the `<Tabs>` component from:

```html
<Tabs default-value="sales">
```

to:

```html
<Tabs :default-value="defaultTab">
```

**Step 3: Verify navigation from /machines "Refill" button opens stock tab**

Run dev server, go to `/machines`, click "Refill →" on a machine card with low stock.
Expected: Detail page opens with "Trays & Stock" tab active.

**Step 4: Commit**

```bash
git add management-frontend/app/pages/machines/[id].vue
git commit -m "feat: support ?tab=stock query param on machine detail page"
```

---

### Task 6: Add Low-Tray Visual Focus on Detail Page

**Files:**
- Modify: `management-frontend/app/pages/machines/[id].vue` (tray cards section)

**Step 1: Find the mobile tray card rendering section**

In the Trays & Stock `<TabsContent>`, find the mobile card layout (the section with `class="space-y-3 md:hidden"`). Each tray card is rendered in a `v-for` loop.

**Step 2: Add conditional dimming class to tray cards**

On each tray card's outer `<div>`, add conditional opacity. Trays that are well-stocked (not low, not empty) when the page was opened via `?tab=stock` should be dimmed:

```typescript
// Add this computed in the <script setup>:
const isRefillMode = computed(() => route.query.tab === 'stock')
```

Then on each mobile tray card div and desktop table row, add:

```html
:class="{ 'opacity-40': isRefillMode && tray.current_stock > tray.min_stock && tray.current_stock > 0 }"
```

This dims healthy trays so the operator's eye is drawn to the ones needing attention. The tray is still visible and interactive (not hidden), just less prominent.

**Step 3: Add "Done — back to machines" link**

Below the tray section (inside the `<TabsContent value="trays">` but after the tray cards), add:

```html
                <!-- Done navigation (visible in refill mode) -->
                <div v-if="isRefillMode" class="mt-6 flex justify-center">
                  <NuxtLink
                    to="/machines"
                    class="inline-flex h-10 items-center justify-center rounded-md border px-6 text-sm font-medium hover:bg-muted"
                  >
                    ← Done — back to machines
                  </NuxtLink>
                </div>
```

**Step 4: Verify the visual focus works**

Navigate to a machine via `/machines/[id]?tab=stock`:
- Low/empty trays should be fully visible
- Well-stocked trays should be dimmed (40% opacity)
- "Done" link should appear at the bottom

Navigate to a machine directly via `/machines/[id]`:
- All trays should have full opacity (no dimming)
- No "Done" link

**Step 5: Commit**

```bash
git add management-frontend/app/pages/machines/[id].vue
git commit -m "feat: add refill mode with low-tray focus and done navigation"
```

---

### Task 7: Final Verification & Cleanup

**Step 1: Test the full operator workflow end-to-end**

1. Navigate to `/machines`
2. Verify machines are sorted by stock urgency (critical first)
3. Verify cards show stock health dot, stock bar, packing list
4. Tap "Refill →" on a machine with low stock
5. Verify detail page opens on Trays & Stock tab
6. Verify low trays are prominent, healthy trays are dimmed
7. Tap "Fill to full" on a low tray — verify it updates
8. Tap "Done — back to machines" — verify it returns to list
9. Verify the machine card's stock status has updated

**Step 2: Test real-time updates**

1. Open `/machines` in one browser tab
2. In a second tab, go to a machine detail page and refill a tray
3. Verify the first tab's machine card updates automatically

**Step 3: Test edge cases**

- Machine with zero trays configured → shows "No trays configured"
- Machine with all trays at capacity → shows compact "All stocked" card
- Machine with all trays empty → shows critical (red) status
- Direct navigation to `/machines/[id]` (no query param) → Sales tab, no dimming

**Step 4: Commit any fixes, then final commit**

```bash
git add -A
git commit -m "chore: final verification and cleanup for stock-first machines page"
```
