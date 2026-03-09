<script setup lang="ts">
import {
  IconDashboard,
  IconDevices,
  IconPackage,
  IconBuildingWarehouse,
  IconMenu2,
} from '@tabler/icons-vue'
import { useSidebar } from '@/components/ui/sidebar'

const route = useRoute()
const { toggleSidebar } = useSidebar()

const tabs = [
  { label: 'Dashboard', icon: IconDashboard, to: '/' },
  { label: 'Machines', icon: IconDevices, to: '/machines' },
  { label: 'Products', icon: IconPackage, to: '/products' },
  { label: 'Warehouse', icon: IconBuildingWarehouse, to: '/warehouse' },
]

function isActive(to: string) {
  if (to === '/') return route.path === '/'
  return route.path.startsWith(to)
}

// "More" is active when the current route is not one of the tab routes
const isMoreActive = computed(() => !tabs.some((t) => isActive(t.to)))
</script>

<template>
  <nav class="fixed bottom-0 left-0 right-0 z-50 border-t bg-background pb-[env(safe-area-inset-bottom)] md:hidden">
    <div class="grid grid-cols-5">
      <NuxtLink
        v-for="tab in tabs"
        :key="tab.to"
        :to="tab.to"
        class="flex flex-col items-center gap-0.5 py-2 text-[11px] transition-colors"
        :class="isActive(tab.to) ? 'text-primary font-medium' : 'text-muted-foreground'"
      >
        <component :is="tab.icon" class="size-5" :stroke-width="isActive(tab.to) ? 2.2 : 1.5" />
        <span>{{ tab.label }}</span>
      </NuxtLink>
      <button
        class="flex flex-col items-center gap-0.5 py-2 text-[11px] transition-colors"
        :class="isMoreActive ? 'text-primary font-medium' : 'text-muted-foreground'"
        @click="toggleSidebar()"
      >
        <IconMenu2 class="size-5" :stroke-width="isMoreActive ? 2.2 : 1.5" />
        <span>More</span>
      </button>
    </div>
  </nav>
</template>
