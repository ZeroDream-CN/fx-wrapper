import { ref, onMounted } from 'vue'

const UPDATE_API = 'https://cfdx.zerodream.net/fivem/fxwrapper/?v=0.0.0'

export function useUpdateInfo() {
  const version = ref('')
  const updateNotes = ref('')
  const downloadUrl = ref('')
  const loading = ref(true)
  const error = ref('')

  async function fetchUpdateInfo() {
    loading.value = true
    error.value = ''

    try {
      const response = await fetch(UPDATE_API)
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`)
      }

      const data = await response.json()
      version.value = data.version ?? ''
      updateNotes.value = data.update ?? ''
      const template = data.url ?? ''
      downloadUrl.value = template.replace('{VERSION}', version.value)
    } catch (err) {
      error.value = err instanceof Error ? err.message : '获取更新信息失败'
    } finally {
      loading.value = false
    }
  }

  onMounted(fetchUpdateInfo)

  return {
    version,
    updateNotes,
    downloadUrl,
    loading,
    error,
    retry: fetchUpdateInfo,
  }
}
