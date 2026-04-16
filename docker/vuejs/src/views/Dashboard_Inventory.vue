<template>

<div class="p-6 space-y-6">

  <!-- HEADER -->
  <div>
    <h1 class="text-2xl font-bold text-gray-800">Inventory</h1>
    <p class="text-gray-500">Stock levels across all machines and coils</p>
  </div>

  <!-- KPI CARDS -->
  <div class="grid grid-cols-3 gap-4">

    <div class="bg-white rounded-xl shadow p-5">
      <p class="text-xs text-gray-400 uppercase tracking-wide mb-1">Products Tracked</p>
      <p class="text-3xl font-bold text-gray-800">{{ stats.products }}</p>
    </div>

    <div class="bg-white rounded-xl shadow p-5">
      <p class="text-xs text-orange-400 uppercase tracking-wide mb-1">Low Stock</p>
      <p class="text-3xl font-bold text-orange-600">{{ stats.low }}</p>
    </div>

    <div class="bg-white rounded-xl shadow p-5">
      <p class="text-xs text-red-400 uppercase tracking-wide mb-1">Empty Coils</p>
      <p class="text-3xl font-bold text-red-600">{{ stats.empty }}</p>
    </div>

  </div>

  <!-- LOADING -->
  <div v-if="loading" class="text-center text-gray-400 py-16 text-sm">Loading inventory...</div>

  <!-- BY PRODUCT TABLE -->
  <div v-else class="bg-white rounded-xl shadow overflow-hidden">

    <div class="px-5 py-4 border-b flex items-center justify-between">
      <h2 class="font-semibold text-gray-700">By Product</h2>
      <span class="text-xs text-gray-400">Click a row to see per-machine breakdown</span>
    </div>

    <table class="w-full text-sm">

      <thead class="bg-gray-50">
        <tr class="text-left text-gray-500">
          <th class="p-4">Product</th>
          <th class="p-4 w-40 text-center">Machines</th>
          <th class="p-4 w-48 text-center">Fill Level</th>
          <th class="p-4 w-24 text-center">Coils</th>
          <th class="p-4 w-28 text-center">Warehouse</th>
          <th class="p-4 w-24 text-center">Status</th>
        </tr>
      </thead>

      <tbody>

        <template v-for="row in byProduct" :key="row.product_id">

          <!-- PRODUCT ROW -->
          <tr
            class="border-t hover:bg-gray-50 cursor-pointer"
            @click="toggleExpand(row.product_id)"
          >

            <td class="p-4 font-medium text-gray-800 flex items-center gap-3">
              <span class="text-gray-400 text-xs w-4">
                {{ expanded === row.product_id ? '▾' : '▸' }}
              </span>
              <img
                v-if="row.image_url"
                :src="row.image_url"
                class="w-7 h-7 rounded object-cover bg-gray-100"
              />
              <span class="w-7 h-7 rounded bg-gray-100 flex items-center justify-center text-gray-400 text-xs" v-else>?</span>
              {{ row.product_name }}
            </td>

            <td class="p-4 text-center">
              <span class="font-semibold text-gray-700">{{ row.total_stock }}</span>
              <span class="text-gray-400"> / {{ row.total_capacity }}</span>
            </td>

            <td class="p-4">
              <div class="flex items-center gap-2">
                <div class="flex-1 h-2 bg-gray-200 rounded-full overflow-hidden">
                  <div
                    :class="barClass(row.pct)"
                    :style="{ width: row.pct + '%' }"
                    class="h-full rounded-full transition-all"
                  />
                </div>
                <span class="text-xs text-gray-500 w-8 text-right">{{ row.pct }}%</span>
              </div>
            </td>

            <td class="p-4 text-center text-gray-500">
              {{ row.coil_count }}
            </td>

            <td class="p-4 text-center">
              <span
                class="text-sm font-semibold"
                :class="row.warehouse_stock === 0 ? 'text-red-600' : row.warehouse_stock <= 10 ? 'text-orange-500' : 'text-gray-700'"
              >
                {{ row.warehouse_stock }}
              </span>
            </td>

            <td class="p-4 text-center">
              <span :class="statusBadgeClass(row)" class="text-xs font-medium px-2 py-0.5 rounded-full">
                {{ statusLabel(row) }}
              </span>
            </td>

          </tr>

          <!-- EXPANDED: PER-MACHINE BREAKDOWN -->
          <tr v-if="expanded === row.product_id" class="bg-slate-50">
            <td colspan="5" class="px-10 pb-4 pt-2">

              <table class="w-full text-xs">
                <thead>
                  <tr class="text-gray-400 border-b">
                    <th class="py-1 text-left">Machine</th>
                    <th class="py-1 text-center">Coil</th>
                    <th class="py-1 text-center">Stock</th>
                    <th class="py-1 text-center">Capacity</th>
                    <th class="py-1 text-center">Fill</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="coil in coilsForProduct(row.product_id)"
                    :key="coil.id"
                    class="border-b border-gray-100"
                  >
                    <td class="py-1.5 text-gray-700 font-medium">{{ coil.machine_name }}</td>
                    <td class="py-1.5 text-center text-gray-500">{{ coil.alias }}</td>
                    <td class="py-1.5 text-center font-semibold" :class="coilStockClass(coil)">
                      {{ coil.current_stock }}
                    </td>
                    <td class="py-1.5 text-center text-gray-400">{{ coil.capacity }}</td>
                    <td class="py-1.5 text-center">
                      <div class="flex items-center gap-1 justify-center">
                        <div class="w-16 h-1.5 bg-gray-200 rounded-full overflow-hidden">
                          <div
                            :class="barClass(coilPct(coil))"
                            :style="{ width: coilPct(coil) + '%' }"
                            class="h-full rounded-full"
                          />
                        </div>
                        <span class="text-gray-400 w-7 text-right">{{ coilPct(coil) }}%</span>
                      </div>
                    </td>
                  </tr>
                </tbody>
              </table>

            </td>
          </tr>

        </template>

        <tr v-if="byProduct.length === 0">
          <td colspan="5" class="p-8 text-center text-gray-400">
            No inventory data. Assign products to coils first.
          </td>
        </tr>

      </tbody>

    </table>

  </div>

</div>

</template>

<script>
import { supabase } from '@/lib/supabase'
import { lowStockThreshold } from '@/lib/settings'

export default {
  name: 'Inventory',

  data() {
    return {
      loading: false,
      coils: [],
      expanded: null
    }
  },

  computed: {
    byProduct() {
      const map = {}

      for (const c of this.coils) {
        if (!c.product_id) continue
        if (!map[c.product_id]) {
          map[c.product_id] = {
            product_id:      c.product_id,
            product_name:    c.products?.name ?? '—',
            image_url:       c.products?.image_url ?? null,
            warehouse_stock: c.products?.current_stock ?? 0,
            total_stock:     0,
            total_capacity:  0,
            coil_count:      0
          }
        }
        const row = map[c.product_id]
        row.total_stock    += c.current_stock ?? 0
        row.total_capacity += c.capacity ?? 0
        row.coil_count     += 1
      }

      return Object.values(map)
        .map(r => ({
          ...r,
          pct: r.total_capacity > 0
            ? Math.round((r.total_stock / r.total_capacity) * 100)
            : 0
        }))
        .sort((a, b) => a.pct - b.pct)  // critical first
    },

    stats() {
      return {
        products: this.byProduct.length,
        low:   this.byProduct.filter(r => r.pct > 0  && r.pct <= lowStockThreshold.value).length,
        empty: this.coils.filter(c => (c.current_stock ?? 0) === 0 && c.product_id).length
      }
    }
  },

  mounted() {
    this.loadInventory()
  },

  methods: {
    async loadInventory() {
      this.loading = true

      const { data, error } = await supabase
        .from('machine_coils')
        .select('id, alias, item_number, capacity, current_stock, product_id, machine_id, products(id, name, image_url, current_stock), machines(id, name)')
        .not('product_id', 'is', null)
        .order('alias')

      if (!error) {
        this.coils = data.map(c => ({
          ...c,
          machine_name: c.machines?.name ?? '—'
        }))
      }

      this.loading = false
    },

    coilsForProduct(productId) {
      return this.coils
        .filter(c => c.product_id === productId)
        .sort((a, b) => (a.machine_name > b.machine_name ? 1 : -1))
    },

    toggleExpand(productId) {
      this.expanded = this.expanded === productId ? null : productId
    },

    coilPct(coil) {
      if (!coil.capacity) return 0
      return Math.round(((coil.current_stock ?? 0) / coil.capacity) * 100)
    },

    barClass(pct) {
      const t = lowStockThreshold.value
      if (pct === 0)    return 'bg-red-500'
      if (pct <= t)     return 'bg-orange-400'
      if (pct <= t * 2) return 'bg-yellow-400'
      return 'bg-green-500'
    },

    coilStockClass(coil) {
      const pct = this.coilPct(coil)
      const t = lowStockThreshold.value
      if (pct === 0)  return 'text-red-600'
      if (pct <= t)   return 'text-orange-600'
      return 'text-gray-700'
    },

    statusLabel(row) {
      const t = lowStockThreshold.value
      if (row.pct === 0)    return 'Empty'
      if (row.pct <= t)     return 'Low'
      if (row.pct <= t * 2) return 'Medium'
      return 'OK'
    },

    statusBadgeClass(row) {
      const t = lowStockThreshold.value
      if (row.pct === 0)    return 'bg-red-100 text-red-700'
      if (row.pct <= t)     return 'bg-orange-100 text-orange-700'
      if (row.pct <= t * 2) return 'bg-yellow-100 text-yellow-700'
      return 'bg-green-100 text-green-700'
    }
  }
}
</script>
