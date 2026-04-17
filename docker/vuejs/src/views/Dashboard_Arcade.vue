<template>
<div class="flex flex-col items-center justify-center h-full bg-gray-100 p-6">

  <div class="mb-4 text-center">
    <h1 class="text-2xl font-bold text-gray-800">Arcade</h1>
    <p class="text-sm text-gray-400 mt-1">Take a break — you earned it</p>
  </div>

  <iframe
    src="/arcade/index.html"
    class="rounded-xl border-2 border-slate-700 shadow-2xl"
    style="width: 860px; max-width: 100%; height: 380px; background: #0f172a;"
    frameborder="0"
    allowfullscreen
  />

  <p class="mt-4 text-xs text-gray-400">
    Tip: you can also open the game anywhere by typing <kbd class="px-1.5 py-0.5 bg-slate-200 rounded text-gray-600 font-mono text-xs">vmflow</kbd>
  </p>

</div>
</template>
