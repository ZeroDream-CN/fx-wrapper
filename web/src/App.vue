<script setup>
import { BookOpen, Download, Workflow } from '@lucide/vue'
import { computed } from 'vue'
import IntroSection from './components/IntroSection.vue'
import DownloadSection from './components/DownloadSection.vue'
import HowItWorksSection from './components/HowItWorksSection.vue'
import ContentDisclaimer from './components/ContentDisclaimer.vue'
import { useHashNav } from './composables/useHashNav'

const navItems = [
  { id: 'intro', label: '介绍', icon: BookOpen, component: IntroSection },
  { id: 'download', label: '下载', icon: Download, component: DownloadSection },
  { id: 'how-it-works', label: '原理', icon: Workflow, component: HowItWorksSection },
]

const navIds = navItems.map((item) => item.id)
const { activeId, navigate } = useHashNav(navIds)

const activeItem = computed(() => navItems.find((item) => item.id === activeId.value) ?? navItems[0])
</script>

<template>
  <div class="min-h-screen md:flex md:h-full md:justify-center md:overflow-hidden">
    <div
      class="mx-auto flex w-full max-w-6xl flex-1 flex-col px-4 py-1 sm:px-6 md:h-full md:min-h-0 md:px-8 md:py-8 lg:max-w-6xl"
    >
      <header class="shrink-0 md:hidden">
        <div class="flex items-center gap-2.5 py-4">
          <div>
            <h1 class="text-base font-medium text-neutral-900">FXWrapper</h1>
            <p class="mt-0.5 text-xs text-neutral-400">FiveM 服务端沙盒绕过补丁</p>
          </div>
        </div>
        <nav class="flex gap-1 pb-1">
          <button
            v-for="item in navItems"
            :key="item.id"
            type="button"
            class="flex flex-1 items-center justify-center gap-1.5 rounded py-2.5 text-sm transition"
            :class="
              activeId === item.id
                ? 'bg-neutral-100 font-medium text-neutral-900'
                : 'text-neutral-400'
            "
            @click="navigate(item.id)"
          >
            <component :is="item.icon" class="h-3.5 w-3.5 shrink-0" />
            {{ item.label }}
          </button>
        </nav>
      </header>

      <div class="flex min-h-0 flex-1 flex-col md:flex-row md:gap-12 md:overflow-hidden lg:gap-16">
        <aside class="hidden shrink-0 flex-col md:flex md:w-44 lg:w-48">
          <div class="flex items-center gap-2.5 px-1 py-5">
            <div>
              <h1 class="text-base font-medium text-neutral-900">FXWrapper</h1>
              <p class="mt-0.5 text-xs text-neutral-400">FiveM 服务端沙盒绕过补丁</p>
            </div>
          </div>

          <nav class="flex-1 px-1 py-2">
            <button
              v-for="item in navItems"
              :key="item.id"
              type="button"
              class="mb-0.5 flex w-full items-center gap-2.5 rounded px-3 py-2 text-left text-sm transition"
              :class="
                activeId === item.id
                  ? 'bg-neutral-100 font-medium text-neutral-900'
                  : 'text-neutral-500 hover:bg-neutral-50 hover:text-neutral-800'
              "
              @click="navigate(item.id)"
            >
              <component
                :is="item.icon"
                class="h-4 w-4 shrink-0"
                :class="activeId === item.id ? 'text-neutral-700' : 'text-neutral-400'"
              />
              {{ item.label }}
            </button>
          </nav>
        </aside>

        <main class="flex min-h-0 min-w-0 flex-1 flex-col">
          <div class="scrollbar-hidden min-h-0 flex-1 overflow-y-auto py-5 md:py-5">
            <Transition name="fade" mode="out-in">
              <component :is="activeItem.component" :key="activeId" />
            </Transition>

            <ContentDisclaimer />
          </div>
        </main>
      </div>
    </div>
  </div>
</template>

<style scoped>
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.15s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
