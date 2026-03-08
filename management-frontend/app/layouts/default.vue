<script setup lang="ts">
import AppSidebar from '@/components/AppSidebar.vue'
import SiteHeader from '@/components/SiteHeader.vue'
import { SidebarInset, SidebarProvider } from '@/components/ui/sidebar'

const { showBanner, isIOS, canInstall, dismiss, promptInstall } = useInstallPrompt()
const { updateAvailable, applyUpdate } = useAppUpdate()
</script>

<template>
  <SidebarProvider
    :style="{
      '--sidebar-width': 'calc(var(--spacing) * 72)',
      '--header-height': 'calc(var(--spacing) * 12)',
    }"
  >
    <AppSidebar variant="inset" />
    <SidebarInset>
      <SiteHeader />

      <!-- App update toast -->
      <Transition
        enter-active-class="transition duration-300 ease-out"
        enter-from-class="-translate-y-full opacity-0"
        enter-to-class="translate-y-0 opacity-100"
        leave-active-class="transition duration-200 ease-in"
        leave-from-class="translate-y-0 opacity-100"
        leave-to-class="-translate-y-full opacity-0"
      >
        <div
          v-if="updateAvailable"
          class="mx-4 mt-2 flex items-center justify-between gap-3 rounded-lg border border-primary/20 bg-primary/5 px-4 py-2.5 text-sm"
        >
          <div class="flex items-center gap-2">
            <svg class="h-4 w-4 shrink-0 text-primary" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0 3.181 3.183a8.25 8.25 0 0 0 13.803-3.7M4.031 9.865a8.25 8.25 0 0 1 13.803-3.7l3.181 3.182" />
            </svg>
            <span>New version available</span>
          </div>
          <button
            class="shrink-0 rounded-md bg-primary px-3 py-1 text-xs font-medium text-primary-foreground shadow-sm transition-colors hover:bg-primary/90"
            @click="applyUpdate"
          >
            Update now
          </button>
        </div>
      </Transition>

      <slot />

      <!-- PWA install banner -->
      <Transition
        enter-active-class="transition duration-300 ease-out"
        enter-from-class="translate-y-full opacity-0"
        enter-to-class="translate-y-0 opacity-100"
        leave-active-class="transition duration-200 ease-in"
        leave-from-class="translate-y-0 opacity-100"
        leave-to-class="translate-y-full opacity-0"
      >
        <div
          v-if="showBanner"
          class="fixed bottom-0 left-0 right-0 z-50 border-t bg-card px-4 pb-[env(safe-area-inset-bottom)] md:hidden"
        >
          <div class="flex items-center gap-3 py-3">
            <img src="/icons/icon-192x192.png" alt="VMflow" class="h-10 w-10 shrink-0 rounded-xl" />
            <div class="min-w-0 flex-1">
              <p class="text-sm font-medium">Install VMflow</p>
              <p v-if="isIOS" class="text-xs text-muted-foreground">
                Tap
                <svg class="inline-block h-4 w-4 align-text-bottom" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" d="M9 8.25H7.5a2.25 2.25 0 0 0-2.25 2.25v9a2.25 2.25 0 0 0 2.25 2.25h9a2.25 2.25 0 0 0 2.25-2.25v-9a2.25 2.25 0 0 0-2.25-2.25H15m0-3-3-3m0 0-3 3m3-3V15" /></svg>
                then "Add to Home Screen"
              </p>
              <p v-else class="text-xs text-muted-foreground">Add to your home screen for quick access</p>
            </div>
            <button
              v-if="canInstall"
              class="shrink-0 rounded-md bg-primary px-3 py-1.5 text-xs font-medium text-primary-foreground shadow-sm transition-colors hover:bg-primary/90"
              @click="promptInstall"
            >
              Install
            </button>
            <button
              class="shrink-0 p-1 text-muted-foreground transition-colors hover:text-foreground"
              aria-label="Dismiss"
              @click="dismiss"
            >
              <svg class="h-4 w-4" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor">
                <path stroke-linecap="round" stroke-linejoin="round" d="M6 18 18 6M6 6l12 12" />
              </svg>
            </button>
          </div>
        </div>
      </Transition>
    </SidebarInset>
  </SidebarProvider>
</template>
