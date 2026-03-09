<script setup lang="ts">
import {
  IconDotsVertical,
  IconLogout,
  IconUserCircle,
} from "@tabler/icons-vue"

import {
  Avatar,
  AvatarFallback,
} from '@/components/ui/avatar'
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu'
import {
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
  useSidebar,
} from '@/components/ui/sidebar'

const supabase = useSupabaseClient()
const user = useSupabaseUser()
const { isMobile, setOpenMobile } = useSidebar()

// @nuxtjs/supabase v2 returns JWT claims (sub) not User object (id)
const userId = computed(() => user.value?.id ?? (user.value as any)?.sub ?? null)

const firstName = ref('')
const lastName = ref('')

const email = computed(() => user.value?.email ?? '')

const displayName = computed(() => {
  const parts = [firstName.value, lastName.value].filter(Boolean)
  return parts.length > 0 ? parts.join(' ') : ''
})

const initials = computed(() => {
  // Prefer name-based initials, fall back to email
  if (firstName.value || lastName.value) {
    return [firstName.value, lastName.value]
      .filter(Boolean)
      .map(p => p[0]?.toUpperCase() ?? '')
      .join('') || 'U'
  }
  const parts = email.value.split('@')[0].split('.')
  return parts.slice(0, 2).map(p => p[0]?.toUpperCase() ?? '').join('') || 'U'
})

async function loadUserName() {
  if (!userId.value) return
  const { data } = await supabase
    .from('users')
    .select('first_name, last_name')
    .eq('id', userId.value)
    .single()
  if (data) {
    firstName.value = (data as any).first_name ?? ''
    lastName.value = (data as any).last_name ?? ''
  }
}

watch(userId, (uid) => { if (import.meta.client && uid) loadUserName() }, { immediate: true })

async function logout() {
  await supabase.auth.signOut()
  await navigateTo('/auth/login')
}
</script>

<template>
  <SidebarMenu>
    <SidebarMenuItem>
      <DropdownMenu>
        <DropdownMenuTrigger as-child>
          <SidebarMenuButton
            size="lg"
            class="data-[state=open]:bg-sidebar-accent data-[state=open]:text-sidebar-accent-foreground"
          >
            <Avatar class="h-8 w-8 rounded-lg grayscale">
              <AvatarFallback class="rounded-lg">{{ initials }}</AvatarFallback>
            </Avatar>
            <div class="grid flex-1 text-left text-sm leading-tight">
              <span v-if="displayName" class="truncate font-medium">{{ displayName }}</span>
              <span class="truncate" :class="displayName ? 'text-xs text-muted-foreground' : 'font-medium'">{{ email }}</span>
            </div>
            <IconDotsVertical class="ml-auto size-4" />
          </SidebarMenuButton>
        </DropdownMenuTrigger>
        <DropdownMenuContent
          class="w-(--reka-dropdown-menu-trigger-width) min-w-56 rounded-lg"
          :side="isMobile ? 'bottom' : 'right'"
          :side-offset="4"
          align="end"
        >
          <DropdownMenuLabel class="p-0 font-normal">
            <div class="flex items-center gap-2 px-1 py-1.5 text-left text-sm">
              <Avatar class="h-8 w-8 rounded-lg">
                <AvatarFallback class="rounded-lg">{{ initials }}</AvatarFallback>
              </Avatar>
              <div class="grid flex-1 text-left text-sm leading-tight">
                <span v-if="displayName" class="truncate font-medium">{{ displayName }}</span>
                <span class="truncate" :class="displayName ? 'text-xs text-muted-foreground' : 'font-medium'">{{ email }}</span>
              </div>
            </div>
          </DropdownMenuLabel>
          <DropdownMenuSeparator />
          <DropdownMenuGroup>
            <DropdownMenuItem as-child @click="isMobile && setOpenMobile(false)">
              <NuxtLink to="/settings">
                <IconUserCircle />
                Account
              </NuxtLink>
            </DropdownMenuItem>
          </DropdownMenuGroup>
          <DropdownMenuSeparator />
          <DropdownMenuItem @click="logout">
            <IconLogout />
            Log out
          </DropdownMenuItem>
        </DropdownMenuContent>
      </DropdownMenu>
    </SidebarMenuItem>
  </SidebarMenu>
</template>
