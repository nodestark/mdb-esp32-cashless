export default defineNuxtRouteMiddleware(async (to) => {
  const publicRoutes = [
    '/auth/login',
    '/auth/register',
    '/onboarding/create-organization',
    '/onboarding/accept-invitation',
  ]
  if (publicRoutes.some(route => to.path.startsWith(route))) {
    return
  }

  const user = useSupabaseUser()
  if (!user.value) {
    return navigateTo('/auth/login')
  }

  // Use cached organization state when available
  const organization = useState<any>('organization')
  if (organization.value !== null && organization.value !== undefined) {
    return
  }

  const supabase = useSupabaseClient()
  try {
    const { data, error } = await supabase.functions.invoke('get-my-organization')
    if (error) throw error
    organization.value = data?.organization ?? null
    if (!data?.organization) {
      return navigateTo('/onboarding/create-organization')
    }
  } catch {
    return navigateTo('/onboarding/create-organization')
  }
})
