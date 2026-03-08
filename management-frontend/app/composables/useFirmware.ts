export interface FirmwareVersion {
  id: string
  created_at: string
  company_id: string
  version_label: string
  file_path: string
  file_size: number | null
  notes: string | null
  uploaded_by: string | null
  source_type: string | null
  source_tag: string | null
}

export interface GitHubRelease {
  tag_name: string
  name: string
  published_at: string
  body: string | null
  html_url: string
  assets: GitHubAsset[]
}

export interface GitHubAsset {
  name: string
  size: number
  browser_download_url: string
}

export function useFirmware() {
  const supabase = useSupabaseClient()
  const { organization } = useOrganization()

  const firmwareVersions = ref<FirmwareVersion[]>([])
  const loading = ref(false)

  // ── GitHub releases ────────────────────────────────────────────────────
  const config = useRuntimeConfig()
  const githubRepo = computed(() => config.public.githubFirmwareRepo as string)
  const githubReleases = ref<GitHubRelease[]>([])
  const githubLoading = ref(false)

  async function fetchFirmwareVersions() {
    loading.value = true
    try {
      const { data, error } = await supabase
        .from('firmware_versions')
        .select('*')
        .order('created_at', { ascending: false })

      if (error) throw error
      firmwareVersions.value = (data as FirmwareVersion[]) ?? []
    } finally {
      loading.value = false
    }
  }

  async function uploadFirmware(file: File, versionLabel: string, notes?: string) {
    if (!organization.value) throw new Error('No organization')

    const filePath = `${organization.value.id}/${versionLabel}.bin`

    // Upload binary to storage
    const { error: uploadError } = await supabase.storage
      .from('firmware')
      .upload(filePath, file, {
        upsert: true,
        contentType: 'application/octet-stream',
      })
    if (uploadError) throw uploadError

    // Create database record
    const { error: insertError } = await supabase
      .from('firmware_versions')
      .insert({
        company_id: organization.value.id,
        version_label: versionLabel,
        file_path: filePath,
        file_size: file.size,
        notes: notes || null,
      })
    if (insertError) {
      // Clean up the uploaded file if DB insert fails
      await supabase.storage.from('firmware').remove([filePath])
      throw insertError
    }

    await fetchFirmwareVersions()
  }

  async function triggerOta(deviceId: string, firmwareId: string) {
    const { data, error } = await supabase.functions.invoke('trigger-ota', {
      body: { device_id: deviceId, firmware_id: firmwareId },
    })
    if (error) throw error
    if (data?.error) throw new Error(data.error)
    return data
  }

  async function deleteFirmwareVersion(id: string, filePath: string) {
    await supabase.storage.from('firmware').remove([filePath])
    const { error } = await supabase
      .from('firmware_versions')
      .delete()
      .eq('id', id)
    if (error) throw error
    await fetchFirmwareVersions()
  }

  // ── GitHub release integration ─────────────────────────────────────────

  async function fetchGitHubReleases() {
    if (!githubRepo.value) return
    githubLoading.value = true
    try {
      const res = await $fetch<GitHubRelease[]>(
        `https://api.github.com/repos/${githubRepo.value}/releases`,
        { headers: { Accept: 'application/vnd.github.v3+json' } }
      )
      // Only show releases that have .bin assets
      githubReleases.value = res.filter(r =>
        r.assets.some(a => a.name.endsWith('.bin'))
      )
    } catch (e) {
      console.error('Failed to fetch GitHub releases:', e)
      githubReleases.value = []
    } finally {
      githubLoading.value = false
    }
  }

  async function importGitHubRelease(
    tag: string,
    assetName: string,
    versionLabel?: string,
    notes?: string,
  ) {
    const { data, error } = await supabase.functions.invoke('import-github-release', {
      body: { tag, asset_name: assetName, version_label: versionLabel, notes },
    })
    if (error) throw error
    if (data?.error) throw new Error(data.error)
    await fetchFirmwareVersions()
    return data
  }

  /** Check if a GitHub release tag has already been imported */
  function isReleaseImported(tag: string): boolean {
    return firmwareVersions.value.some(fw => fw.source_tag === tag)
  }

  return {
    firmwareVersions,
    loading,
    fetchFirmwareVersions,
    uploadFirmware,
    triggerOta,
    deleteFirmwareVersion,
    // GitHub integration
    githubRepo,
    githubReleases,
    githubLoading,
    fetchGitHubReleases,
    importGitHubRelease,
    isReleaseImported,
  }
}
