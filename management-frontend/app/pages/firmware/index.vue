<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import { timeAgo } from '@/lib/utils'

const { role } = useOrganization()
const {
  firmwareVersions, loading, fetchFirmwareVersions,
  uploadFirmware, triggerOta, deleteFirmwareVersion,
  githubRepo, githubReleases, githubLoading,
  fetchGitHubReleases, importGitHubRelease, isReleaseImported,
} = useFirmware()
const { machines, fetchMachines } = useMachines()

const isAdmin = computed(() => role.value === 'admin')

onMounted(async () => {
  await Promise.all([
    fetchFirmwareVersions(),
    fetchMachines(),
    fetchGitHubReleases(),
  ])
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

// ── GitHub import ────────────────────────────────────────────────────────────
const importLoading = ref<string | null>(null)
const importError = ref('')
const importSuccess = ref('')

async function handleImport(tag: string, assetName: string) {
  importLoading.value = tag
  importError.value = ''
  importSuccess.value = ''
  try {
    await importGitHubRelease(tag, assetName)
    importSuccess.value = `Imported ${assetName} from ${tag}`
    // Refresh to pick up the new firmware version
    await fetchGitHubReleases()
  } catch (err: unknown) {
    importError.value = err instanceof Error ? err.message : 'Import failed'
  } finally {
    importLoading.value = null
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
            <th class="px-4 py-3 font-medium">Source</th>
            <th class="px-4 py-3 font-medium">Size</th>
            <th class="hidden md:table-cell px-4 py-3 font-medium">Notes</th>
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
            <td class="px-4 py-3">
              <span
                v-if="fw.source_type === 'github'"
                class="inline-flex items-center gap-1 rounded-full bg-purple-100 px-2 py-0.5 text-xs font-medium text-purple-700 dark:bg-purple-900/30 dark:text-purple-300"
              >
                <svg class="h-3 w-3" viewBox="0 0 16 16" fill="currentColor"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z" /></svg>
                GitHub
              </span>
              <span
                v-else
                class="inline-flex items-center rounded-full bg-blue-100 px-2 py-0.5 text-xs font-medium text-blue-700 dark:bg-blue-900/30 dark:text-blue-300"
              >
                Upload
              </span>
            </td>
            <td class="px-4 py-3 text-muted-foreground">{{ formatSize(fw.file_size) }}</td>
            <td class="hidden md:table-cell px-4 py-3 text-muted-foreground truncate max-w-xs">{{ fw.notes ?? '—' }}</td>
            <td class="px-4 py-3 text-muted-foreground">
              <span :title="formatDate(fw.created_at)">{{ timeAgo(fw.created_at) }}</span>
            </td>
            <td v-if="isAdmin" class="px-4 py-3">
              <div class="flex items-center gap-3">
                <button
                  class="text-xs text-primary hover:underline"
                  @click="openOtaModal(fw.id)"
                >
                  Deploy
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

    <!-- GitHub Releases section -->
    <template v-if="githubRepo">
      <div class="flex items-center justify-between pt-4">
        <div>
          <h2 class="text-lg font-semibold">GitHub Releases</h2>
          <p class="text-sm text-muted-foreground">
            Import firmware builds from
            <a
              :href="`https://github.com/${githubRepo}/releases`"
              target="_blank"
              rel="noopener"
              class="text-primary hover:underline"
            >{{ githubRepo }}</a>
          </p>
        </div>
        <button
          class="inline-flex h-8 items-center justify-center rounded-md border px-3 text-xs font-medium shadow-sm transition-colors hover:bg-muted"
          :disabled="githubLoading"
          @click="fetchGitHubReleases"
        >
          {{ githubLoading ? 'Loading...' : 'Refresh' }}
        </button>
      </div>

      <!-- Import status messages -->
      <p v-if="importError" class="text-sm text-destructive">{{ importError }}</p>
      <p v-if="importSuccess" class="text-sm text-green-600 dark:text-green-400">{{ importSuccess }}</p>

      <div v-if="githubLoading && githubReleases.length === 0" class="text-muted-foreground text-sm">
        Loading releases from GitHub...
      </div>

      <div v-else-if="githubReleases.length === 0" class="text-muted-foreground text-sm">
        No releases with firmware binaries found.
      </div>

      <div v-else class="rounded-md border">
        <table class="w-full text-sm">
          <thead>
            <tr class="border-b bg-muted/50 text-left">
              <th class="px-4 py-3 font-medium">Release</th>
              <th class="px-4 py-3 font-medium">Asset</th>
              <th class="hidden md:table-cell px-4 py-3 font-medium">Size</th>
              <th class="px-4 py-3 font-medium">Published</th>
              <th v-if="isAdmin" class="px-4 py-3 font-medium">Action</th>
            </tr>
          </thead>
          <tbody>
            <template v-for="release in githubReleases" :key="release.tag_name">
              <tr
                v-for="asset in release.assets.filter(a => a.name.endsWith('.bin'))"
                :key="`${release.tag_name}-${asset.name}`"
                class="border-b last:border-0 hover:bg-muted/30 transition-colors"
              >
                <td class="px-4 py-3">
                  <a
                    :href="release.html_url"
                    target="_blank"
                    rel="noopener"
                    class="font-mono font-medium text-primary hover:underline"
                  >{{ release.tag_name }}</a>
                  <p v-if="release.name && release.name !== release.tag_name" class="text-xs text-muted-foreground truncate max-w-[200px]">
                    {{ release.name }}
                  </p>
                </td>
                <td class="px-4 py-3 font-mono text-xs text-muted-foreground">{{ asset.name }}</td>
                <td class="hidden md:table-cell px-4 py-3 text-muted-foreground">{{ formatSize(asset.size) }}</td>
                <td class="px-4 py-3 text-muted-foreground">
                  <span :title="formatDate(release.published_at)">{{ timeAgo(release.published_at) }}</span>
                </td>
                <td v-if="isAdmin" class="px-4 py-3">
                  <button
                    v-if="isReleaseImported(release.tag_name)"
                    disabled
                    class="inline-flex h-7 items-center rounded-md border px-3 text-xs font-medium text-muted-foreground opacity-60"
                  >
                    Imported
                  </button>
                  <button
                    v-else
                    class="inline-flex h-7 items-center rounded-md bg-primary px-3 text-xs font-medium text-primary-foreground shadow-sm transition-colors hover:bg-primary/90 disabled:opacity-50"
                    :disabled="importLoading === release.tag_name"
                    @click="handleImport(release.tag_name, asset.name)"
                  >
                    {{ importLoading === release.tag_name ? 'Importing...' : 'Import' }}
                  </button>
                </td>
              </tr>
            </template>
          </tbody>
        </table>
      </div>
    </template>
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
