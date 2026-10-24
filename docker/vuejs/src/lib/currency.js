import { ref, computed } from 'vue'
import { supabase } from '@/lib/supabase'

// Operator currency: stored per operator in credentials (key "currency"), since
// sales amounts carry no currency and every viewer of an account must see the
// same one. The browser language only picks the number format and, until the
// operator saves a choice, a first guess from its region.

export const CURRENCIES = ['USD', 'BRL', 'EUR', 'GBP', 'MXN', 'CAD', 'AUD', 'CLP', 'ARS', 'COP', 'PEN', 'CHF', 'JPY']

const REGION_CURRENCY = {
  US: 'USD', BR: 'BRL', GB: 'GBP', MX: 'MXN', CA: 'CAD', AU: 'AUD', CL: 'CLP',
  AR: 'ARS', CO: 'COP', PE: 'PEN', CH: 'CHF', JP: 'JPY',
  DE: 'EUR', FR: 'EUR', ES: 'EUR', IT: 'EUR', PT: 'EUR', NL: 'EUR', BE: 'EUR',
  AT: 'EUR', IE: 'EUR', FI: 'EUR', GR: 'EUR', BG: 'EUR', HR: 'EUR', SK: 'EUR',
  SI: 'EUR', LT: 'EUR', LV: 'EUR', EE: 'EUR', LU: 'EUR', MT: 'EUR', CY: 'EUR',
}

const locale = (typeof navigator !== 'undefined' && navigator.language) || 'en-US'

export function guessCurrency() {
  try {
    const region = new Intl.Locale(locale).maximize().region
    return REGION_CURRENCY[region] ?? 'USD'
  } catch {
    return 'USD'
  }
}

export const currency = ref(guessCurrency())

export function formatMoney(value, code = currency.value) {
  return new Intl.NumberFormat(locale, { style: 'currency', currency: code }).format(value ?? 0)
}

export function currencySymbolOf(code) {
  return new Intl.NumberFormat(locale, { style: 'currency', currency: code })
    .formatToParts(0).find(p => p.type === 'currency')?.value ?? code
}

export const currencySymbol = computed(() => currencySymbolOf(currency.value))

export async function loadCurrency() {
  const { data } = await supabase
    .from('credentials')
    .select('value')
    .eq('key', 'currency')
    .maybeSingle()

  if (data?.value) currency.value = data.value
}

export async function saveCurrency(code) {
  const { error } = await supabase
    .from('credentials')
    .upsert({ key: 'currency', value: code }, { onConflict: 'owner_id,key' })

  if (!error) currency.value = code
  return error
}
