<template>

<div class="p-6 space-y-4">

  <!-- HEADER -->
  <div class="flex justify-between items-center">
    <div>
      <h1 class="text-2xl font-bold text-gray-800">Map</h1>
      <p class="text-gray-500">Machine locations and sales activity</p>
    </div>

    <div class="flex items-center gap-3">
      <label class="flex items-center gap-2 text-sm text-gray-600 cursor-pointer select-none">
        <input type="checkbox" v-model="showSales" class="accent-slate-700" />
        Show sales
      </label>
      <button
        @click="fitBounds"
        class="px-3 py-2 text-sm bg-slate-800 hover:bg-slate-700 text-white rounded transition"
      >
        Fit all
      </button>
    </div>
  </div>

  <!-- LEGEND -->
  <div class="flex items-center gap-5 text-xs text-gray-500">
    <span class="flex items-center gap-1.5"><span class="w-3 h-3 rounded-full bg-green-500 inline-block"></span> Online</span>
    <span class="flex items-center gap-1.5"><span class="w-3 h-3 rounded-full bg-red-500 inline-block"></span> Offline / no device</span>
    <span class="flex items-center gap-1.5"><span class="w-3 h-3 rounded-full bg-orange-400 inline-block"></span> Low stock</span>
    <span class="flex items-center gap-1.5"><span class="w-2 h-2 rounded-full bg-blue-400 inline-block opacity-60"></span> Sale</span>
  </div>

  <!-- MAP -->
  <div class="bg-white rounded-xl shadow overflow-hidden">
    <div ref="mapEl" class="w-full" style="height: 520px;"></div>
  </div>

  <!-- NO LOCATION WARNING -->
  <div v-if="noLocationMachines.length" class="bg-yellow-50 border border-yellow-200 rounded-lg px-4 py-3 text-sm text-yellow-800">
    <span class="font-medium">No location set:</span>
    {{ noLocationMachines.map(m => m.name).join(', ') }} — update lat/lng in the database.
  </div>

</div>

</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import { supabase } from '@/lib/supabase'
import { lowStockThreshold } from '@/lib/settings'
import L from 'leaflet'
import 'leaflet/dist/leaflet.css'

const mapEl = ref(null)
const showSales = ref(true)
const machines = ref([])
const sales = ref([])

let map = null
let machineLayerGroup = null
let salesLayerGroup = null

const noLocationMachines = computed(() =>
  machines.value.filter(m => m.lat == null || m.lng == null)
)

function machinePct(machine) {
  const coils = machine.machine_coils ?? []
  const stock = coils.reduce((s, c) => s + (c.current_stock ?? 0), 0)
  const cap   = coils.reduce((s, c) => s + (c.capacity ?? 0), 0)
  return cap > 0 ? Math.round((stock / cap) * 100) : null
}

function markerColor(machine) {
  const online = machine.embedded?.status === 'online'
  if (!online) return '#ef4444'
  const pct = machinePct(machine)
  if (pct === null) return '#6b7280'
  if (pct === 0)                          return '#ef4444'
  if (pct <= lowStockThreshold.value)     return '#fb923c'
  return '#22c55e'
}

function makeIcon(machine) {
  const color = markerColor(machine)
  const pct   = machinePct(machine)
  const label = pct !== null ? `${pct}%` : '?'

  return L.divIcon({
    className: '',
    iconAnchor: [22, 44],
    popupAnchor: [0, -44],
    html: `
      <div style="display:flex;flex-direction:column;align-items:center;gap:2px">
        <div style="
          background:${color};
          color:#fff;
          font-size:11px;
          font-weight:700;
          width:44px;height:44px;
          border-radius:50% 50% 50% 0;
          transform:rotate(-45deg);
          display:flex;align-items:center;justify-content:center;
          box-shadow:0 2px 6px rgba(0,0,0,.35);
          border:2px solid #fff;
        ">
          <span style="transform:rotate(45deg)">${label}</span>
        </div>
      </div>
    `
  })
}

function popupHtml(machine) {
  const pct    = machinePct(machine)
  const online = machine.embedded?.status === 'online'
  const color  = markerColor(machine)
  const bar    = pct !== null
    ? `<div style="background:#e5e7eb;border-radius:4px;height:6px;margin-top:4px">
         <div style="width:${pct}%;height:100%;border-radius:4px;background:${color}"></div>
       </div>
       <p style="font-size:11px;color:#6b7280;margin:2px 0 0">${pct}% fill level</p>`
    : '<p style="font-size:11px;color:#9ca3af;margin:4px 0 0">No coils configured</p>'

  const statusBadge = online
    ? '<span style="background:#dcfce7;color:#15803d;font-size:10px;padding:1px 6px;border-radius:99px">online</span>'
    : '<span style="background:#fee2e2;color:#b91c1c;font-size:10px;padding:1px 6px;border-radius:99px">offline</span>'

  return `
    <div style="min-width:160px;font-family:sans-serif">
      <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:6px">
        <strong style="font-size:13px;color:#1f2937">${machine.name}</strong>
        ${statusBadge}
      </div>
      ${bar}
      <p style="font-size:10px;color:#d1d5db;margin:6px 0 0">${machine.lat?.toFixed(5)}, ${machine.lng?.toFixed(5)}</p>
    </div>
  `
}

function renderMachines() {
  machineLayerGroup.clearLayers()

  for (const m of machines.value) {
    if (m.lat == null || m.lng == null) continue
    L.marker([m.lat, m.lng], { icon: makeIcon(m) })
      .bindPopup(popupHtml(m), { maxWidth: 220 })
      .addTo(machineLayerGroup)
  }
}

function renderSales() {
  salesLayerGroup.clearLayers()
  if (!showSales.value) return

  for (const s of sales.value) {
    if (s.lat == null || s.lng == null) continue
    L.circleMarker([s.lat, s.lng], {
      radius: 5,
      fillColor: '#60a5fa',
      fillOpacity: 0.5,
      color: '#3b82f6',
      weight: 1
    }).addTo(salesLayerGroup)
  }
}

function fitBounds() {
  const points = machines.value
    .filter(m => m.lat != null && m.lng != null)
    .map(m => [m.lat, m.lng])

  if (points.length === 0) return
  if (points.length === 1) {
    map.setView(points[0], 14)
  } else {
    map.fitBounds(L.latLngBounds(points), { padding: [48, 48] })
  }
}

watch(showSales, renderSales)

async function load() {
  const [{ data: mData }, { data: sData }] = await Promise.all([
    supabase
      .from('machines')
      .select('id, name, lat, lng, embedded(status), machine_coils(capacity, current_stock)'),
    supabase
      .from('sales')
      .select('lat, lng')
      .not('lat', 'is', null)
      .not('lng', 'is', null)
      .limit(2000)
      .order('created_at', { ascending: false })
  ])

  machines.value = mData ?? []
  sales.value    = sData ?? []

  renderMachines()
  renderSales()
  fitBounds()
}

onMounted(() => {
  map = L.map(mapEl.value, { zoomControl: true }).setView([-15.8, -47.9], 5)

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '© <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
  }).addTo(map)

  machineLayerGroup = L.layerGroup().addTo(map)
  salesLayerGroup   = L.layerGroup().addTo(map)

  load()
})

onBeforeUnmount(() => {
  map?.remove()
})
</script>
