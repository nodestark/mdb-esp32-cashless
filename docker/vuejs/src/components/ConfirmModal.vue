<template>

<div v-if="open" class="fixed inset-0 bg-black/40 flex items-center justify-center z-50">
  <div class="bg-white p-6 rounded-xl w-96">

    <h2 class="text-lg font-semibold mb-2">{{ title }}</h2>

    <p class="text-gray-600 mb-6">
      <slot>{{ message }}</slot>
    </p>

    <div class="flex justify-end gap-2">
      <button
        @click="$emit('cancel')"
        :disabled="loading"
        class="px-3 py-1 border rounded text-sm disabled:opacity-50"
      >
        {{ cancelText }}
      </button>
      <button
        @click="$emit('confirm')"
        :disabled="loading"
        :class="danger ? 'bg-red-600 hover:bg-red-500' : 'bg-slate-800 hover:bg-slate-700'"
        class="px-3 py-1 text-white text-sm rounded transition disabled:opacity-50"
      >
        {{ loading ? confirmingText : confirmText }}
      </button>
    </div>

  </div>
</div>

</template>

<script setup>
defineProps({
  open: { type: Boolean, default: false },
  title: { type: String, required: true },
  message: { type: String, default: '' },
  confirmText: { type: String, default: 'Delete' },
  confirmingText: { type: String, default: 'Deleting...' },
  cancelText: { type: String, default: 'Cancel' },
  loading: { type: Boolean, default: false },
  danger: { type: Boolean, default: true }
})

defineEmits(['confirm', 'cancel'])
</script>
