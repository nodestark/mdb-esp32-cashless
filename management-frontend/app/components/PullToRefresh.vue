<script setup lang="ts">
import { IconLoader2 } from '@tabler/icons-vue'

const THRESHOLD = 60 // px to pull before triggering refresh
const MAX_PULL = 120 // cap visual pull distance
const RESISTANCE = 0.45 // dampen finger movement for natural feel

const refreshHandler = useState<(() => Promise<void> | void) | null>('pull-refresh-handler', () => null)

const containerRef = ref<HTMLElement | null>(null)
const pulling = ref(false)
const refreshing = ref(false)
const pullDistance = ref(0)
const startY = ref(0)
const canPull = ref(false)

function isAtTop(): boolean {
  // Check if any scrollable ancestor is scrolled to top
  // Walk up the DOM to find the actual scrollable container
  let el: HTMLElement | null = containerRef.value
  while (el) {
    if (el.scrollTop > 0) return false
    el = el.parentElement
  }
  return window.scrollY <= 0
}

function onTouchStart(e: TouchEvent) {
  if (refreshing.value || !refreshHandler.value) return
  startY.value = e.touches[0].clientY
  canPull.value = isAtTop()
}

function onTouchMove(e: TouchEvent) {
  if (!canPull.value || refreshing.value) return

  const currentY = e.touches[0].clientY
  const diff = (currentY - startY.value) * RESISTANCE

  if (diff <= 0) {
    // Scrolling up — reset
    pulling.value = false
    pullDistance.value = 0
    return
  }

  // Prevent browser scroll while pulling
  e.preventDefault()
  pulling.value = true
  pullDistance.value = Math.min(diff, MAX_PULL)
}

async function onTouchEnd() {
  if (!pulling.value) return

  if (pullDistance.value >= THRESHOLD && refreshHandler.value) {
    // Trigger refresh
    refreshing.value = true
    pullDistance.value = THRESHOLD // snap to threshold height during refresh
    try {
      await refreshHandler.value()
    } catch (err) {
      console.warn('[PullToRefresh] Refresh handler error:', err)
    } finally {
      refreshing.value = false
    }
  }

  // Animate back to 0
  pulling.value = false
  pullDistance.value = 0
  canPull.value = false
}

const indicatorOpacity = computed(() =>
  Math.min(pullDistance.value / THRESHOLD, 1),
)

const indicatorRotation = computed(() =>
  refreshing.value ? 0 : Math.min((pullDistance.value / THRESHOLD) * 180, 180),
)
</script>

<template>
  <div
    ref="containerRef"
    class="relative"
    @touchstart.passive="onTouchStart"
    @touchmove="onTouchMove"
    @touchend.passive="onTouchEnd"
  >
    <!-- Pull indicator -->
    <div
      class="pointer-events-none absolute inset-x-0 top-0 z-40 flex justify-center transition-opacity duration-150"
      :class="pulling || refreshing ? 'opacity-100' : 'opacity-0'"
      :style="{ transform: `translateY(${pullDistance - 40}px)` }"
    >
      <div
        class="flex h-9 w-9 items-center justify-center rounded-full border bg-background shadow-md"
        :style="{ opacity: indicatorOpacity }"
      >
        <IconLoader2
          class="size-4 text-muted-foreground"
          :class="refreshing ? 'animate-spin' : ''"
          :style="refreshing ? {} : { transform: `rotate(${indicatorRotation}deg)` }"
        />
      </div>
    </div>

    <!-- Content with pull translation -->
    <div
      :style="{
        transform: pullDistance > 0 ? `translateY(${pullDistance}px)` : '',
        transition: pulling ? 'none' : 'transform 0.3s ease-out',
      }"
    >
      <slot />
    </div>
  </div>
</template>
