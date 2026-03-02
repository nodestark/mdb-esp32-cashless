<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { timeAgo } from '@/lib/utils'

const { role } = useOrganization()
const { firmwareVersions, loading, fetchFirmwareVersions, uploadFirmware, triggerOta, deleteFirmwareVersion } = useFirmware()
const { machines, fetchMachines } = useMachines()

const isAdmin = computed(() => role.value === 'admin')

onMounted(async () => {
  await Promise.all([fetchFirmwareVersions(), fetchMachines()])
})

// ── Upload modal ─────────────────────────────────────────────────────────────
const showUploadModal = ref(false)
const uploadForm = ref({ versionLabel: '', notes: '' })
const uploadFile = ref<File | null>(null)
const uploadLoading = ref(false)
const uploadError = ref('')

function openUploadModal() {
  uploadForm.value = { versionLabel: '', notes: '' }
  uploadFile.value = null
  uploadError.value = ''
  showUploadModal.value = true
}

function onFileChange(e: Event) {
  const input = e.target as HTMLInputElement
  uploadFile.value = input.files?.[0] ?? null
}

async function submitUpload() {
  if (!uploadFile.value) {
    uploadError.value = 'Please select a firmware binary file'
    return
  }
  if (!uploadForm.value.versionLabel.trim()) {
    uploadError.value = 'Version label is required'
    return
  }
  uploadLoading.value = true
  uploadError.value = ''
  try {
    await uploadFirmware(uploadFile.value, uploadForm.value.versionLabel.trim(), uploadForm.value.notes.trim() || undefined)
    showUploadModal.value = false
  } catch (err: unknown) {
    uploadError.value = err instanceof Error ? err.message : 'Upload failed'
  } finally {
    uploadLoading.value = false
  }
}

// ── OTA trigger modal ────────────────────────────────────────────────────────
const showOtaModal = ref(false)
const selectedFirmwareId = ref('')
const selectedDeviceId = ref('')
const otaLoading = ref(false)
const otaError = ref('')
const otaSuccess = ref('')

function openOtaModal(firmwareId: string) {
  selectedFirmwareId.value = firmwareId
  selectedDeviceId.value = ''
  otaError.value = ''
  otaSuccess.value = ''
  showOtaModal.value = true
}

// Get devices (embeddeds) from machines
const onlineDevices = computed(() => {
  return machines.value
    .filter((m: any) => m.embeddeds?.id)
    .map((m: any) => ({
      id: m.embeddeds.id,
      name: m.name ?? 'Unnamed',
      status: m.embeddeds.status,
      mac: m.embeddeds.mac_address,
      firmware_version: m.embeddeds.firmware_version ?? null,
      firmware_build_date: m.embeddeds.firmware_build_date ?? null,
    }))
})

async function submitOta() {
  if (!selectedDeviceId.value) {
    otaError.value = 'Please select a device'
    return
  }
  otaLoading.value = true
  otaError.value = ''
  otaSuccess.value = ''
  try {
    const result = await triggerOta(selectedDeviceId.value, selectedFirmwareId.value)
    otaSuccess.value = `OTA update triggered (device ${result.status})`
  } catch (err: unknown) {
    otaError.value = err instanceof Error ? err.message : 'Failed to trigger OTA'
  } finally {
    otaLoading.value = false
  }
}

// ── Delete ───────────────────────────────────────────────────────────────────
const deleteLoading = ref<string | null>(null)

async function handleDelete(fw: any) {
  deleteLoading.value = fw.id
  try {
    await deleteFirmwareVersion(fw.id, fw.file_path)
  } catch {
    // silent
  } finally {
    deleteLoading.value = null
  }
}

// ── Helpers ──────────────────────────────────────────────────────────────────
function formatSize(bytes: number | null) {
  if (bytes == null) return '—'
  if (bytes < 1024) return `${bytes} B`
  const kb = bytes / 1024
  if (kb < 1024) return `${kb.toFixed(1)} KB`
  return `${(kb / 1024).toFixed(2)} MB`
}

function formatDate(dt: string) {
  return new Date(dt).toLocaleString()
}


</script>

<template>
  <div class="flex flex-1 flex-col gap-4 p-4 md:p-6">
    <div class="flex items-center justify-between">
      <h1 class="text-2xl font-semibold">Firmware</h1>
      <button
        v-if="isAdmin"
        class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
        @click="openUploadModal"
      >
        Upload Firmware
      </button>
    </div>

    <div v-if="loading" class="text-muted-foreground">Loading firmware versions...</div>

    <div v-else-if="firmwareVersions.length === 0" class="text-muted-foreground">
      No firmware versions uploaded yet. Upload a .bin firmware file to get started.
    </div>

    <!-- Firmware versions table -->
    <div v-else class="rounded-md border">
      <table class="w-full text-sm">
        <thead>
          <tr class="border-b bg-muted/50 text-left">
            <th class="px-4 py-3 font-medium">Version</th>
            <th class="px-4 py-3 font-medium">Size</th>
            <th class="px-4 py-3 font-medium">Notes</th>
            <th class="px-4 py-3 font-medium">Uploaded</th>
            <th v-if="isAdmin" class="px-4 py-3 font-medium">Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="fw in firmwareVersions"
            :key="fw.id"
            class="border-b last:border-0 hover:bg-muted/30 transition-colors"
          >
            <td class="px-4 py-3 font-mono font-medium">{{ fw.version_label }}</td>
            <td class="px-4 py-3 text-muted-foreground">{{ formatSize(fw.file_size) }}</td>
            <td class="px-4 py-3 text-muted-foreground truncate max-w-xs">{{ fw.notes ?? '—' }}</td>
            <td class="px-4 py-3 text-muted-foreground">
              <span :title="formatDate(fw.created_at)">{{ timeAgo(fw.created_at) }}</span>
            </td>
            <td v-if="isAdmin" class="px-4 py-3">
              <div class="flex items-center gap-3">
                <button
                  class="text-xs text-primary hover:underline"
                  @click="openOtaModal(fw.id)"
                >
                  Deploy to device
                </button>
                <button
                  class="text-xs text-destructive hover:underline"
                  :disabled="deleteLoading === fw.id"
                  @click="handleDelete(fw)"
                >
                  {{ deleteLoading === fw.id ? 'Deleting...' : 'Delete' }}
                </button>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>

  <!-- Upload firmware modal -->
  <div
    v-if="showUploadModal"
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    @click.self="showUploadModal = false"
  >
    <div class="w-full max-w-md rounded-xl border bg-card p-6 shadow-lg">
      <h2 class="mb-1 text-lg font-semibold">Upload Firmware</h2>
      <p class="mb-5 text-sm text-muted-foreground">
        Upload a compiled firmware binary (.bin) for OTA deployment to your devices.
      </p>
      <form class="space-y-4" @submit.prevent="submitUpload">
        <div class="space-y-1">
          <label class="text-sm font-medium" for="fw-version">Version label</label>
          <input
            id="fw-version"
            v-model="uploadForm.versionLabel"
            type="text"
            placeholder="e.g. 1.2.0"
            required
            class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
          />
        </div>
        <div class="space-y-1">
          <label class="text-sm font-medium" for="fw-file">Firmware binary</label>
          <input
            id="fw-file"
            type="file"
            accept=".bin,application/octet-stream"
            class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors file:border-0 file:bg-transparent file:text-sm file:font-medium placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
            @change="onFileChange"
          />
          <p class="text-xs text-muted-foreground">Max 5 MB. ESP32 firmware binary.</p>
        </div>
        <div class="space-y-1">
          <label class="text-sm font-medium" for="fw-notes">Notes</label>
          <input
            id="fw-notes"
            v-model="uploadForm.notes"
            type="text"
            placeholder="e.g. Fixed WiFi reconnection bug"
            class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
          />
        </div>
        <p v-if="uploadError" class="text-sm text-destructive">{{ uploadError }}</p>
        <div class="flex gap-2">
          <button
            type="button"
            class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
            @click="showUploadModal = false"
          >
            Cancel
          </button>
          <button
            type="submit"
            :disabled="uploadLoading"
            class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
          >
            <span v-if="uploadLoading">Uploading...</span>
            <span v-else>Upload</span>
          </button>
        </div>
      </form>
    </div>
  </div>

  <!-- OTA trigger modal -->
  <div
    v-if="showOtaModal"
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    @click.self="showOtaModal = false"
  >
    <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
      <h2 class="mb-1 text-lg font-semibold">Deploy Firmware</h2>
      <p class="mb-4 text-sm text-muted-foreground">
        Select a device to receive this firmware update over-the-air. The device must be online.
      </p>
      <form class="space-y-4" @submit.prevent="submitOta">
        <div class="space-y-1">
          <label class="text-sm font-medium" for="ota-device">Target device</label>
          <select
            id="ota-device"
            v-model="selectedDeviceId"
            class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
          >
            <option value="" disabled>Select a device</option>
            <option v-for="d in onlineDevices" :key="d.id" :value="d.id">
              {{ d.name }} — {{ d.mac ?? 'No MAC' }} ({{ d.status }}{{ d.firmware_version ? `, v${d.firmware_version}` : '' }}{{ d.firmware_build_date ? `, built ${new Date(d.firmware_build_date).toLocaleString()}` : '' }})
            </option>
          </select>
          <p v-if="onlineDevices.length === 0" class="text-xs text-muted-foreground">No devices with assigned machines found.</p>
        </div>
        <p v-if="otaError" class="text-sm text-destructive">{{ otaError }}</p>
        <p v-if="otaSuccess" class="text-sm text-green-600 dark:text-green-400">{{ otaSuccess }}</p>
        <div class="flex gap-2">
          <button
            type="button"
            class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
            @click="showOtaModal = false"
          >
            {{ otaSuccess ? 'Close' : 'Cancel' }}
          </button>
          <button
            v-if="!otaSuccess"
            type="submit"
            :disabled="otaLoading || !selectedDeviceId"
            class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
          >
            <span v-if="otaLoading">Deploying...</span>
            <span v-else>Deploy</span>
          </button>
        </div>
      </form>
    </div>
  </div>
</template>
