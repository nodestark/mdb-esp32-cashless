import { ref, watch } from 'vue'

const LOW_STOCK_KEY = 'vmflow_low_stock_threshold'

export const lowStockThreshold = ref(
  parseInt(localStorage.getItem(LOW_STOCK_KEY) ?? '25', 10)
)

watch(lowStockThreshold, val => {
  localStorage.setItem(LOW_STOCK_KEY, val)
})
