<script setup lang="ts">
import { IconMoon, IconSun } from '@tabler/icons-vue'
import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'
import { SidebarTrigger } from '@/components/ui/sidebar'

const { isDark, toggleTheme } = useTheme()
</script>

<template>
  <header class="flex h-(--header-height) shrink-0 items-center gap-2 border-b transition-[width,height] ease-linear group-has-data-[collapsible=icon]/sidebar-wrapper:h-(--header-height)">
    <div class="flex w-full items-center gap-1 px-4 lg:gap-2 lg:px-6">
      <SidebarTrigger class="-ml-1" />
      <Separator
        orientation="vertical"
        class="mx-2 data-[orientation=vertical]:h-4"
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
