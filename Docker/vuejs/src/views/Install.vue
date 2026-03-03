<template>
  <div class="min-h-screen bg-gray-50 flex flex-col items-center px-6 py-10">

    <!-- IMAGEM -->
    <img
      src="/install/mdb-slave-esp32s3_pcb_v3.jpg"
      alt="VMflow Board"
      class="w-64 rounded-2xl shadow-lg my-6"
    />

    <!-- TÍTULO -->
    <h1 class="text-3xl font-semibold text-gray-800 text-center">
      Install VMflow Firmware
    </h1>

    <!-- INSTRUÇÕES -->
    <div class="max-w-lg text-gray-700 text-base leading-relaxed mt-4 mb-6 space-y-2">
      <p><strong>1)</strong> Plug in your ESP to a USB port. We will install <strong>VMflow v1.0.0</strong> to it.</p>
      <p><strong>2)</strong> Hit <strong>"Install"</strong> and select the correct COM port.</p>
      <p><strong>3)</strong> Get VMflow installed and connected in less than 3 minutes!</p>
    </div>

    <!-- SELECT -->
        <div class="flex items-center gap-3 mb-6">
      <span class="text-gray-700">
        Firmware Version:
      </span>

      <select
        v-model="selectedManifest"
        class="px-3 py-2 rounded-md border border-gray-300 bg-white
               focus:outline-none focus:ring-2 focus:ring-blue-500 transition"
      >
        <option
          v-for="build in builds"
          :key="build.manifest"
          :value="build.manifest"
        >
          {{ build.version.toUpperCase() }}
        </option>
      </select>
    </div>

    <!-- BOTÃO ESP -->

    <esp-web-install-button id="installButton" manifest="nightly/manifest.json" install-supported="">
        <template shadowrootmode="open">
            <slot name="activate"><button>CONNECT</button></slot>
        </template>
    </esp-web-install-button>

    <!-- FOOTER -->
    <div class="mt-auto pt-12 text-sm text-gray-600 text-center space-x-6">
      <a
        href="https://github.com/nodestark/mdb-esp32-cashless"
        target="_blank"
        class="text-blue-600 hover:underline"
      >
        GitHub Project
      </a>

      <a
        href="https://vmflow.xyz/"
        target="_blank"
        class="text-blue-600 hover:underline"
      >
        vmflow.xyz
      </a>
    </div>

  </div>
</template>

<script>
import { ref, onMounted } from 'vue'

export default {
  setup() {
    const builds = ref([])
    const selectedManifest = ref(null)

    const loadVersions = async () => {
      try {
        const response = await fetch('/install/versions.json')
        const data = await response.json()

        builds.value = data.builds || []

        if (builds.value.length > 0) {
          selectedManifest.value = builds.value[0].manifest
        }

      } catch (error) {
        console.error('Erro ao carregar versions.json:', error)
      }
    }

    onMounted(() => {

      const script = document.createElement('script');
      script.src = 'https://unpkg.com/esp-web-tools@3.4.2/dist/web/install-button.js?module';
      document.body.appendChild(script);

      loadVersions()
    })

    return {
      builds,
      selectedManifest
    }
  }
}
</script>