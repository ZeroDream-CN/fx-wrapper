<script setup>
import { BookOpen, Braces, FolderOpen, ShieldOff, Terminal, Users } from '@lucide/vue'

const effects = [
  { icon: Terminal, text: '调用外部程序（如 cmd.exe、独立 Web 服务等）' },
  { icon: FolderOpen, text: '向服务器目录外写入文件' },
  { icon: Terminal, text: 'Lua os.execute 等系统命令' },
  { icon: Users, text: '创建子进程与 Worker' },
  { icon: Braces, text: 'Node.js 脚本的权限限制' },
]
</script>

<template>
  <section>
    <h2 class="mb-6 flex items-center gap-2 text-3xl font-medium text-neutral-900">
      <BookOpen class="h-8 w-8 text-neutral-400" />
      介绍
    </h2>

    <div class="space-y-4 text-sm leading-7 text-neutral-600">
      <p>
        基于众所周知的 “合规” 原因，FiveM 在某次更新之后加入了 Lua/NodeJs 沙盒机制。什么是沙盒呢？简单说，以前你可以在自己服务器上通过 Lua 脚本或者 NodeJs 脚本调用任意外部程序，或者向外部目录写入任意文件，而现在引入沙盒之后，这一切都会被阻止，当你尝试调用 cmd.exe 或者向 C 盘写入一个文件的时候只会返回 Permission denied 错误。
      </p>
      <p>
        对于大部分不会写脚本的服主来说，这个影响没什么所谓，但对于一些热衷于自主开发插件，或者你需要运行某些需要调用外部程序的插件（比如 ZeroDream 车牌插件，需要独立的 Web 服务器程序来确保高性能），这个沙盒限制就是最大的绊脚石。
      </p>
      <p>
        以往的做法一般是回退到旧版本工件，或者通过从源码构建 DLL 然后替换的形式，但前者会导致服务器在列表上不可见，且无法通过 UI 加入服务器（只能通过 F8 直连命令进入），后者则需要不断地去更新代码来适配最新的工件。而这个补丁，则是通过运行时动态 Hook（挂钩）FiveM 服务端来实现的，无需替换任何 DLL，始终兼容最新版本工件，无需担心服务器不出现在列表上。
      </p>

      <img src="https://i.zerodream.net/f85f166a7192b7457aa6c0b0654f5da3.png" alt="沙盒限制" class="w-full h-auto rounded-lg shadow-md">
    </div>

    <h3 class="mb-4 mt-10 flex items-center gap-2 text-lg font-medium text-neutral-900">
      <ShieldOff class="h-5 w-5 text-neutral-400" />
      解除的限制
    </h3>
    <ul class="space-y-3">
      <li
        v-for="item in effects"
        :key="item.text"
        class="flex gap-3 text-sm leading-6 text-neutral-600"
      >
        <component :is="item.icon" class="mt-0.5 h-4 w-4 shrink-0 text-neutral-400" />
        {{ item.text }}
      </li>
    </ul>
  </section>
</template>
