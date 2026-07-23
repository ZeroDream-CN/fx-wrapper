import { onMounted, onUnmounted, ref } from 'vue'

const DEFAULT_SECTION = 'intro'

function normalizeHash(rawHash) {
  const value = rawHash.replace(/^#/, '').trim()
  return value || DEFAULT_SECTION
}

export function useHashNav(validIds, initialId = DEFAULT_SECTION) {
  const activeId = ref(initialId)

  function isValidId(id) {
    return validIds.includes(id)
  }

  function syncFromHash() {
    if (!window.location.hash) {
      window.history.replaceState(null, '', `#${DEFAULT_SECTION}`)
    }

    const hashId = normalizeHash(window.location.hash)
    activeId.value = isValidId(hashId) ? hashId : DEFAULT_SECTION

    if (!isValidId(hashId) && hashId !== DEFAULT_SECTION) {
      window.history.replaceState(null, '', `#${DEFAULT_SECTION}`)
    }
  }

  function navigate(id) {
    if (!isValidId(id)) {
      return
    }

    activeId.value = id
    const nextHash = `#${id}`
    if (window.location.hash !== nextHash) {
      window.history.pushState(null, '', nextHash)
    }
  }

  function onHashChange() {
    syncFromHash()
  }

  onMounted(() => {
    syncFromHash()
    window.addEventListener('hashchange', onHashChange)
  })

  onUnmounted(() => {
    window.removeEventListener('hashchange', onHashChange)
  })

  return {
    activeId,
    navigate,
  }
}
