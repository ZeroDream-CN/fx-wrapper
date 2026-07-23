<script setup>
import {
  AlertTriangle,
  Download,
  FolderArchive,
  ListOrdered,
  Loader2,
  Play,
  RefreshCw,
  Tag,
} from '@lucide/vue'
import { computed } from 'vue'
import { useFileDownload } from '../composables/useFileDownload'
import { useUpdateInfo } from '../composables/useUpdateInfo'

const { version, updateNotes, downloadUrl, loading, error, retry } = useUpdateInfo()
const { downloading, progress, downloadError, downloadToFile } = useFileDownload()

const fallbackFilename = computed(() => `FXWrapper-${version.value || 'latest'}.zip`)

const steps = [
  { icon: Download, text: '下载 FXWrapper 压缩包，并解压文件' },
  { icon: FolderArchive, text: '根据系统选择对应文件，并将 FXWrapper 与 FXServer 放置在同一目录。' },
  {
    icon: Play,
    text: '修改启动脚本，将 FXServer 替换为 FXWrapper，所有传递给 FXWrapper 的参数会原样透传给 FXServer',
  },
]

function handleDownload() {
  if (!downloadUrl.value) {
    return
  }
  downloadToFile(downloadUrl.value, fallbackFilename.value)
}
</script>

<template>
  <section>
    <h2 class="mb-6 flex items-center gap-2 text-3xl font-medium text-neutral-900">
      <Download class="h-8 w-8 text-neutral-400" />
      下载
    </h2>

    <div v-if="loading" class="flex items-center gap-2 text-sm text-neutral-400">
      <Loader2 class="h-4 w-4 animate-spin" />
      加载中…
    </div>

    <div v-else-if="error" class="text-sm">
      <p class="text-neutral-500">无法获取版本信息</p>
      <button
        type="button"
        class="mt-3 inline-flex items-center gap-1.5 text-sm text-neutral-900 underline underline-offset-2"
        @click="retry"
      >
        <RefreshCw class="h-3.5 w-3.5" />
        重试
      </button>
    </div>

    <template v-else>
      <dl class="space-y-4 text-sm">
        <div>
          <dt class="flex items-center gap-1.5 text-neutral-400">
            <Tag class="h-3.5 w-3.5" />
            最新版本
          </dt>
          <dd class="mt-1 text-2xl font-medium tabular-nums text-neutral-900">v{{ version }}</dd>
        </div>
        <div v-if="updateNotes">
          <dt class="text-neutral-400">更新说明</dt>
          <dd class="mt-1 leading-6 text-neutral-600">{{ updateNotes }}</dd>
        </div>
      </dl>

      <button
        type="button"
        class="mt-8 inline-flex items-center gap-2 border border-neutral-900 px-5 py-2.5 text-sm text-neutral-900 transition hover:bg-neutral-900 hover:text-white disabled:cursor-not-allowed disabled:opacity-50"
        :disabled="downloading || !downloadUrl"
        @click="handleDownload"
      >
        <Loader2 v-if="downloading" class="h-4 w-4 animate-spin" />
        <Download v-else class="h-4 w-4" />
        {{ downloading ? '下载中…' : `下载 v${version}` }}
      </button>

      <div v-if="downloading" class="mt-4">
        <div class="h-1.5 w-full overflow-hidden rounded-full bg-neutral-100">
          <div
            class="h-full rounded-full bg-neutral-900 transition-[width] duration-150"
            :style="{ width: `${progress || 8}%` }"
          />
        </div>
        <p class="mt-2 text-xs text-neutral-400">
          {{ progress > 0 ? `已下载 ${progress}%` : '正在连接…' }}
        </p>
      </div>

      <p v-if="downloadError" class="mt-3 text-xs text-red-600">{{ downloadError }}</p>
    </template>

    <h3 class="mb-4 mt-10 flex items-center gap-2 text-sm font-medium text-neutral-900">
      <ListOrdered class="h-4 w-4 text-neutral-400" />
      使用步骤
    </h3>
    <ol class="space-y-4">
      <li
        v-for="(step, index) in steps"
        :key="index"
        class="flex gap-3 text-sm leading-6 text-neutral-600"
      >
        <span
          class="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-neutral-100"
        >
          <component :is="step.icon" class="h-3.5 w-3.5 text-neutral-500" />
        </span>
        <span class="pt-0.5">{{ step.text }}</span>
      </li>
    </ol>

    <div class="mt-8 flex gap-3 rounded-lg bg-yellow-50 p-4 text-sm">
      <AlertTriangle class="mt-0.5 h-4 w-4 shrink-0 text-yellow-600" />
      <p class="text-yellow-800">
        若杀毒软件报毒，选择忽略即可（存在 DLL 注入行为，部分杀软会误报）
      </p>
    </div>
  </section>
</template>
