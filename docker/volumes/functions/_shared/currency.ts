// Operator currency (credentials key "currency", ISO 4217). Defaults to USD,
// which is what Stripe checkouts used before the setting existed.
export const DEFAULT_CURRENCY = 'usd'

// Stripe zero-decimal currencies: amounts are sent in whole units, not cents.
const ZERO_DECIMAL = ['bif', 'clp', 'djf', 'gnf', 'jpy', 'kmf', 'krw', 'mga', 'pyg', 'rwf', 'ugx', 'vnd', 'vuv', 'xaf', 'xof', 'xpf']

export function toStripeAmount(amount: number, currency: string): number {
  return ZERO_DECIMAL.includes(currency) ? Math.round(amount) : Math.round(amount * 100)
}

export function fromStripeAmount(amount: number, currency: string): number {
  return ZERO_DECIMAL.includes(currency) ? amount : amount / 100
}
