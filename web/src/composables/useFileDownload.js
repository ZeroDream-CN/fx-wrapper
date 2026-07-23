import { ref } from 'vue'

function parseFilenameFromUrl(url, fallback) {
  try {
    const pathname = new URL(url).pathname
    const name = pathname.split('/').pop()
    if (name) {
      return decodeURIComponent(name)
    }
  } catch {
    // ignore
  }
  return fallback
}

export function useFileDownload() {
  const downloading = ref(false)
  const progress = ref(0)
  const downloadError = ref('')

  async function downloadToFile(url, fallbackFilename) {
    if (!url || downloading.value) {
      return
    }

    downloading.value = true
    progress.value = 0
    downloadError.value = ''

    try {
      const response = await fetch(url)
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`)
      }

      const totalBytes = Number(response.headers.get('content-length') || 0)
      const reader = response.body?.getReader()

      if (!reader) {
        const blob = await response.blob()
        triggerSave(blob, parseFilenameFromUrl(url, fallbackFilename))
        return
      }

      const chunks = []
      let receivedBytes = 0

      while (true) {
        const { done, value } = await reader.read()
        if (done) {
          break
        }

        chunks.push(value)
        receivedBytes += value.length

        if (totalBytes > 0) {
          progress.value = Math.min(100, Math.round((receivedBytes / totalBytes) * 100))
        }
      }

      const blob = new Blob(chunks)
      if (totalBytes <= 0) {
        progress.value = 100
      }

      triggerSave(blob, parseFilenameFromUrl(url, fallbackFilename))
    } catch (err) {
      downloadError.value = err instanceof Error ? err.message : '下载失败'
    } finally {
      downloading.value = false
      progress.value = 0
    }
  }

  return {
    downloading,
    progress,
    downloadError,
    downloadToFile,
  }
}

function triggerSave(blob, filename) {
  const objectUrl = URL.createObjectURL(blob)
  const anchor = document.createElement('a')
  anchor.href = objectUrl
  anchor.download = filename
  anchor.style.display = 'none'
  document.body.appendChild(anchor)
  anchor.click()
  anchor.remove()
  URL.revokeObjectURL(objectUrl)
}
