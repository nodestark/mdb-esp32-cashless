import { useSupabaseClient } from '#imports'
import * as XLSX from 'xlsx'

export interface ImportProduct {
  name: string
  sellprice: number | null
  category_name: string | null
  image_url: string | null
  selected: boolean
}

export interface ImportResult {
  total: number
  created: number
  skipped: number
  image_errors: number
  errors: string[]
}

export function useImportProducts() {
  const products = ref<ImportProduct[]>([])
  const parsing = ref(false)
  const importing = ref(false)
  const parseError = ref('')
  const importResult = ref<ImportResult | null>(null)

  async function parseFile(file: File): Promise<void> {
    parsing.value = true
    parseError.value = ''
    products.value = []
    importResult.value = null

    try {
      const buffer = await file.arrayBuffer()
      const workbook = XLSX.read(buffer, { type: 'array' })
      const sheetName = workbook.SheetNames[0]
      if (!sheetName) throw new Error('No sheets found in file')
      const sheet = workbook.Sheets[sheetName]
      const rows: Record<string, any>[] = XLSX.utils.sheet_to_json(sheet)

      products.value = rows
        .filter(row => row['Product Name']?.toString().trim())
        .map(row => ({
          name: row['Product Name'].toString().trim(),
          sellprice: parseFloat(row['Product Default Price']) || null,
          category_name: row['Product Group Name']?.toString().trim() || null,
          image_url: row['Product Image URL']?.toString().trim() || null,
          selected: true,
        }))

      if (products.value.length === 0) {
        parseError.value = 'No products found in file. Make sure the file has a "Product Name" column.'
      }
    } catch (err: unknown) {
      parseError.value = err instanceof Error ? err.message : 'Failed to parse file'
    } finally {
      parsing.value = false
    }
  }

  async function executeImport(): Promise<void> {
    importing.value = true
    importResult.value = null

    try {
      const selected = products.value
        .filter(p => p.selected)
        .map(({ selected: _, ...rest }) => rest)

      if (selected.length === 0) return

      const supabase = useSupabaseClient()
      const { data, error } = await supabase.functions.invoke('import-products', {
        body: { products: selected },
      })

      if (error) throw error
      if (data?.error) throw new Error(data.error)

      importResult.value = data as ImportResult
    } catch (err: unknown) {
      importResult.value = {
        total: 0,
        created: 0,
        skipped: 0,
        image_errors: 0,
        errors: [err instanceof Error ? err.message : 'Import failed'],
      }
    } finally {
      importing.value = false
    }
  }

  function toggleAll(checked: boolean) {
    products.value.forEach(p => (p.selected = checked))
  }

  function reset() {
    products.value = []
    parseError.value = ''
    importResult.value = null
  }

  const selectedCount = computed(() => products.value.filter(p => p.selected).length)
  const allSelected = computed(
    () => products.value.length > 0 && products.value.every(p => p.selected)
  )

  return {
    products,
    parsing,
    importing,
    parseError,
    importResult,
    parseFile,
    executeImport,
    toggleAll,
    reset,
    selectedCount,
    allSelected,
  }
}
