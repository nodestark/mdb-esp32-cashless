<script setup lang="ts">
import { SwitchRoot, SwitchThumb } from 'reka-ui'
import { cn } from '@/lib/utils'

const props = defineProps<{
  checked?: boolean
  disabled?: boolean
}>()

const emit = defineEmits<{
  'update:checked': [value: boolean]
}>()
</script>

<template>
  <SwitchRoot
    :checked="props.checked"
    :disabled="props.disabled"
    :class="cn(
      'peer inline-flex h-5 w-9 shrink-0 cursor-pointer items-center rounded-full border-2 border-transparent shadow-sm transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background disabled:cursor-not-allowed disabled:opacity-50',
      props.checked ? 'bg-primary' : 'bg-input',
    )"
    @update:checked="emit('update:checked', $event)"
  >
    <SwitchThumb
      :class="cn(
        'pointer-events-none block h-4 w-4 rounded-full bg-background shadow-lg ring-0 transition-transform',
        props.checked ? 'translate-x-4' : 'translate-x-0',
      )"
    />
  </SwitchRoot>
</template>
