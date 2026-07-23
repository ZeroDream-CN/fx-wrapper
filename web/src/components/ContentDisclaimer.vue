<script setup>
import { Info } from '@lucide/vue'
</script>

<template>
  <p class="mt-12 flex gap-2.5 text-xs leading-relaxed text-neutral-400">
    <Info class="mt-0.5 h-3.5 w-3.5 shrink-0" />
    <span>
      免责声明：本工具通过运行时 Hook 解除 FiveM 服务端沙盒限制，请仅在您拥有合法服务器管理权限、
      且确有需要的情况下使用。因使用本工具产生的任何后果由使用者自行承担。
      讨论帖：
      <a
        href="https://forum.gtaos.com/threads/3907/"
        target="_blank"
        rel="noopener noreferrer"
        class="text-neutral-500 underline underline-offset-2 hover:text-neutral-800"
      >
        GTAOS 论坛
      </a>
    </span>
  </p>
</template>
