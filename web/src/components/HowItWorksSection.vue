<script setup>
import {
  Check,
  Code,
  GitBranch,
  Monitor,
  Play,
  Plug,
  ShieldOff,
  Workflow,
  Wrench,
} from '@lucide/vue'

const steps = [
  { icon: Play, title: '启动', desc: 'FXWrapper 替代 FXServer 启动，以挂起方式创建进程' },
  { icon: Plug, title: '注入', desc: '将 fx-hook 动态库注入 FXServer 进程' },
  { icon: ShieldOff, title: 'Hook', desc: '拦截沙箱权限检查，使文件写入、子进程等检查通过' },
  { icon: Wrench, title: '补丁', desc: 'Hook Lua os.* 与 Node 权限回调，覆盖更多限制点' },
  { icon: GitBranch, title: '传播', desc: 'FXServer 创建子进程时自动注入 Hook，整条进程链均生效' },
]

const details = [
  { icon: Check, text: '无需替换 FiveM 工件中的任何 DLL，兼容最新版本' },
  { icon: Monitor, text: 'Windows：DLL 注入 + MinHook' },
  { icon: Monitor, text: 'Linux：LD_PRELOAD + SubHook' },
  { icon: Code, text: 'citizen-scripting-core 加载后异步安装脚本层 Hook' },
]
</script>

<template>
  <section>
    <h2 class="mb-6 flex items-center gap-2 text-3xl font-medium text-neutral-900">
      <Workflow class="h-8 w-8 text-neutral-400" />
      实现原理
    </h2>

    <p class="text-sm leading-7 text-neutral-600">
      FXWrapper 由启动器（FXWrapper）与 Hook 库（fx-hook）两部分组成。
      启动器负责拉起 FXServer 并注入 Hook 库；Hook 库在运行时拦截 FiveM 的沙箱权限检查，
      从而在不修改工件文件的前提下解除限制。
    </p>

    <h3 class="mb-4 mt-10 text-sm font-medium text-neutral-900">运行流程</h3>
    <ol class="space-y-4">
      <li
        v-for="(step, index) in steps"
        :key="step.title"
        class="flex gap-3 text-sm"
      >
        <span
          class="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-neutral-100"
        >
          <component :is="step.icon" class="h-3.5 w-3.5 text-neutral-500" />
        </span>
        <div class="pt-0.5 leading-6">
          <span class="font-medium text-neutral-900">{{ step.title }}</span>
          <span class="text-neutral-500"> — {{ step.desc }}</span>
        </div>
      </li>
    </ol>

    <h3 class="mb-4 mt-10 text-sm font-medium text-neutral-900">技术要点</h3>
    <ul class="space-y-2">
      <li
        v-for="item in details"
        :key="item.text"
        class="flex gap-3 text-sm leading-6 text-neutral-600"
      >
        <component :is="item.icon" class="mt-0.5 h-4 w-4 shrink-0 text-neutral-400" />
        {{ item.text }}
      </li>
    </ul>
  </section>
</template>
