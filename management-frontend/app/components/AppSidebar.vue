<script setup lang="ts">
import {
  IconCpu,
  IconDashboard,
  IconHelp,
  IconInnerShadowTop,
  IconPackage,
  IconUsers,
  IconDevices,
  IconCloudUpload,
} from "@tabler/icons-vue"

import NavMain from '@/components/NavMain.vue'
import NavSecondary from '@/components/NavSecondary.vue'
import NavUser from '@/components/NavUser.vue'
import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
} from '@/components/ui/sidebar'

const { organization, role } = useOrganization()

const navMain = computed(() => {
  const items = [
    {
      title: "Dashboard",
      url: "/",
      icon: IconDashboard,
    },
    {
      title: "Machines",
      url: "/machines",
      icon: IconDevices,
    },
    {
      title: "Products",
      url: "/products",
      icon: IconPackage,
    },
    {
      title: "Members",
      url: "/members",
      icon: IconUsers,
    },
  ]

  if (role.value === 'admin') {
    items.push(
      {
        title: "Devices",
        url: "/devices",
        icon: IconCpu,
      },
      {
        title: "Firmware",
        url: "/firmware",
        icon: IconCloudUpload,
      },
    )
  }

  return items
})

const navSecondary = [
  {
    title: "Get Help",
    url: "#",
    icon: IconHelp,
  },
]
</script>

<template>
  <Sidebar collapsible="offcanvas">
    <SidebarHeader>
      <SidebarMenu>
        <SidebarMenuItem>
          <SidebarMenuButton
            as-child
            class="data-[slot=sidebar-menu-button]:!p-1.5"
          >
            <NuxtLink to="/">
              <IconInnerShadowTop class="!size-5" />
              <span class="text-base font-semibold">{{ organization?.name ?? 'MDB Dashboard' }}</span>
            </NuxtLink>
          </SidebarMenuButton>
        </SidebarMenuItem>
      </SidebarMenu>
    </SidebarHeader>
    <SidebarContent>
      <NavMain :items="navMain" />
      <NavSecondary :items="navSecondary" class="mt-auto" />
    </SidebarContent>
    <SidebarFooter>
      <NavUser />
    </SidebarFooter>
  </Sidebar>
</template>
