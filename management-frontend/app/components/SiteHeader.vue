<script setup lang="ts">
import { IconMoon, IconSun } from '@tabler/icons-vue'
import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'
import { SidebarTrigger } from '@/components/ui/sidebar'

const { isDark, toggleTheme } = useTheme()
</script>

<template>
  <header class="flex min-h-(--header-height) shrink-0 items-end gap-2 border-b pt-[env(safe-area-inset-top)] transition-[width,height] ease-linear group-has-data-[collapsible=icon]/sidebar-wrapper:min-h-(--header-height)">
    <div class="flex w-full items-center gap-1 px-4 py-2 lg:gap-2 lg:px-6">
      <!-- Sidebar toggle: hidden on mobile where BottomTabBar "More" replaces it -->
      <SidebarTrigger class="-ml-1 hidden md:inline-flex" />
      <Separator
        orientation="vertical"
        class="mx-2 hidden md:block data-[orientation=vertical]:h-4"
      />
      <div class="ml-auto flex items-center gap-2">
        <Button variant="ghost" size="icon" @click="toggleTheme()">
          <IconSun v-if="isDark" class="size-4" />
          <IconMoon v-else class="size-4" />
          <span class="sr-only">Toggle theme</span>
        </Button>
      </div>
    </div>
  </header>
</template>
