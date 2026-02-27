import { useSupabaseClient } from '#imports'

interface Organization {
  id: string
  name: string
  created_at: string
}

export function useOrganization() {
  const organization = useState<Organization | null>('organization', () => null)
  const role = useState<string | null>('org-role', () => null)
  const loading = ref(false)

  async function fetchOrganization() {
    loading.value = true
    try {
      const supabase = useSupabaseClient()
      const { data, error } = await supabase.functions.invoke('get-my-organization')
      if (error) throw error
      organization.value = data.organization ?? null
      role.value = data.role ?? null
    } finally {
      loading.value = false
    }
  }

  return { organization, role, loading, fetchOrganization }
}
