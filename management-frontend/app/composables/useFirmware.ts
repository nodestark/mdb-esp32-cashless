export interface FirmwareVersion {
  id: string
  created_at: string
  company_id: string
  version_label: string
  file_path: string
  file_size: number | null
  notes: string | null
  uploaded_by: string | null
}

export function useFirmware() {
  const supabase = useSupabaseClient()
  const { organization } = useOrganization()

  const firmwareVersions = ref<FirmwareVersion[]>([])
  const loading = ref(false)

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

  return {
    firmwareVersions,
    loading,
    fetchFirmwareVersions,
    uploadFirmware,
    triggerOta,
    deleteFirmwareVersion,
  }
}
