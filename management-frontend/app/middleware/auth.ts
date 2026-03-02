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

  // Skip org fetch on SSR — the Supabase client URL gets rewritten
  // client-side (supabase-url.client.ts), so server-side calls may fail.
  if (import.meta.server) {
    return
  }

  const { organization, role, fetchOrganization } = useOrganization()
  if (organization.value !== null && organization.value !== undefined && role.value !== null) {
    return
  }

  try {
    await fetchOrganization()
    if (!organization.value) {
      return navigateTo('/onboarding/create-organization')
    }
  } catch {
    return navigateTo('/onboarding/create-organization')
  }
})
