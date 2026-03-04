<script setup lang="ts">
import { IconX, IconKeyboard } from '@tabler/icons-vue'

const props = withDefaults(defineProps<{
  formats?: string[]
}>(), {
  formats: () => ['ean_13'],
})

const emit = defineEmits<{
  detected: [barcode: string, format: string]
  close: []
}>()

const videoRef = ref<HTMLVideoElement | null>(null)
const canvasRef = ref<HTMLCanvasElement | null>(null)
const stream = ref<MediaStream | null>(null)
const error = ref('')
const manualInput = ref(false)
const manualBarcode = ref('')
let detecting = false
let animationFrameId: number | null = null

onMounted(async () => {
  await startCamera()
})

onUnmounted(() => {
  stopCamera()
})

async function startCamera() {
  error.value = ''

  // Camera requires secure context (HTTPS or localhost)
  if (!navigator.mediaDevices?.getUserMedia) {
    error.value = 'Camera not available. Use HTTPS or enter the barcode manually.'
    manualInput.value = true
    return
  }

  try {
    stream.value = await navigator.mediaDevices.getUserMedia({
      video: { facingMode: 'environment', width: { ideal: 1280 }, height: { ideal: 720 } },
    })
    if (videoRef.value) {
      videoRef.value.srcObject = stream.value
      await videoRef.value.play()
      startDetection()
    }
  } catch (err: any) {
    if (err.name === 'NotAllowedError') {
      error.value = 'Camera access denied. Please allow camera access or enter the barcode manually.'
    } else if (err.name === 'NotFoundError') {
      error.value = 'No camera found. Please enter the barcode manually.'
    } else {
      error.value = `Camera error: ${err.message}`
    }
    manualInput.value = true
  }
}

function stopCamera() {
  if (animationFrameId) {
    cancelAnimationFrame(animationFrameId)
    animationFrameId = null
  }
  detecting = false
  if (stream.value) {
    stream.value.getTracks().forEach(t => t.stop())
    stream.value = null
  }
}

function startDetection() {
  if (!('BarcodeDetector' in window)) {
    // Fallback: no BarcodeDetector API available
    error.value = 'Barcode scanning not supported in this browser. Please enter the barcode manually.'
    manualInput.value = true
    return
  }

  const detector = new (window as any).BarcodeDetector({
    formats: props.formats,
  })

  detecting = true
  const video = videoRef.value!

  async function detect() {
    if (!detecting || !video || video.readyState < 2) {
      if (detecting) animationFrameId = requestAnimationFrame(detect)
      return
    }

    try {
      const barcodes = await detector.detect(video)
      if (barcodes.length > 0) {
        const barcode = barcodes[0]
        detecting = false
        stopCamera()
        // Audio feedback
        try {
          const ctx = new AudioContext()
          const osc = ctx.createOscillator()
          osc.frequency.value = 1200
          osc.connect(ctx.destination)
          osc.start()
          osc.stop(ctx.currentTime + 0.1)
        } catch {}
        emit('detected', barcode.rawValue, barcode.format)
        return
      }
    } catch {}

    animationFrameId = requestAnimationFrame(detect)
  }

  animationFrameId = requestAnimationFrame(detect)
}

function submitManual() {
  const val = manualBarcode.value.trim()
  if (!val) return
  stopCamera()
  emit('detected', val, 'manual')
}

function close() {
  stopCamera()
  emit('close')
}
</script>

<template>
  <div class="relative rounded-lg border bg-card">
    <!-- Close button -->
    <button
      class="absolute right-2 top-2 z-10 rounded-full bg-black/50 p-1 text-white hover:bg-black/70"
      @click="close"
    >
      <IconX class="size-5" />
    </button>

    <!-- Camera view -->
    <div v-if="!manualInput" class="relative overflow-hidden rounded-t-lg">
      <video
        ref="videoRef"
        class="h-48 w-full object-cover sm:h-56"
        autoplay
        playsinline
        muted
      />
      <!-- Scan overlay -->
      <div class="pointer-events-none absolute inset-0 flex items-center justify-center">
        <div class="h-16 w-3/4 rounded-md border-2 border-white/60" />
      </div>
    </div>

    <!-- Error / manual fallback -->
    <div class="p-3">
      <p v-if="error" class="mb-2 text-xs text-red-600 dark:text-red-400">{{ error }}</p>

      <div v-if="!manualInput" class="flex justify-center">
        <button
          class="inline-flex items-center gap-1 text-xs text-muted-foreground hover:text-foreground"
          @click="manualInput = true"
        >
          <IconKeyboard class="size-3.5" />
          Enter manually
        </button>
      </div>

      <div v-else class="flex gap-2">
        <input
          v-model="manualBarcode"
          type="text"
          placeholder="Enter barcode..."
          class="h-9 flex-1 rounded-md border border-input bg-background px-3 text-sm font-mono focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
          autofocus
          @keydown.enter="submitManual"
        />
        <button
          class="h-9 rounded-md bg-primary px-3 text-sm font-medium text-primary-foreground hover:bg-primary/90"
          @click="submitManual"
        >
          OK
        </button>
      </div>
    </div>
  </div>
</template>
