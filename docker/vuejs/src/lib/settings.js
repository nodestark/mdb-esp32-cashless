import { ref, watch } from 'vue'

const LOW_STOCK_KEY = 'vmflow_low_stock_threshold'
const WAREHOUSE_LOW_STOCK_KEY = 'vmflow_warehouse_low_stock_threshold'

export const lowStockThreshold = ref(
  parseInt(localStorage.getItem(LOW_STOCK_KEY) ?? '25', 10)
)

export const warehouseLowStockThreshold = ref(
  parseInt(localStorage.getItem(WAREHOUSE_LOW_STOCK_KEY) ?? '10', 10)
)

watch(lowStockThreshold, val => {
  localStorage.setItem(LOW_STOCK_KEY, val)
})

watch(warehouseLowStockThreshold, val => {
  localStorage.setItem(WAREHOUSE_LOW_STOCK_KEY, val)
})
