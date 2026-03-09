<template>
  <div class="max-w-6xl mx-auto px-6 py-16 space-y-20">

    <!-- HERO -->
    <section class="grid md:grid-cols-2 gap-10 items-center">

      <div>

        <h1 class="text-4xl md:text-5xl font-bold leading-tight mb-6">
          MDB Protocol for
          <span class="text-slate-700">
            Cashless Vending Machines
          </span>
        </h1>

        <p class="text-gray-600 text-lg mb-8">
          An open-source implementation of the MDB (Multi-Drop Bus) protocol
          using ESP32. Designed for building modern cashless vending systems
          with IoT connectivity and cloud integration.
        </p>

        <div class="flex gap-4">

          <a
            href="https://github.com/nodestark/mdb-esp32-cashless"
            target="_blank"
            class="px-6 py-3 bg-slate-900 text-white rounded-xl shadow hover:bg-slate-800 transition"
          >
            View on GitHub
          </a>

          <RouterLink
            to="/dashboard"
            class="px-6 py-3 border border-slate-900 text-slate-900 rounded-xl hover:bg-slate-900 hover:text-white transition"
          >
            Open Dashboard
          </RouterLink>

        </div>

      </div>

      <!-- HERO IMAGE -->
      <img
        src="/images/Flux_Schnell_A_futuristic_vending_machine_with_bright_yellow_n_2.png"
        class="w-full rounded-2xl shadow-xl"
      />

    </section>

    <!-- HARDWARE IMAGES -->
    <section class="text-center">

      <div class="flex flex-wrap justify-center gap-6">

        <img
          src="/images/1411051686640.jpg"
          class="w-[140px] h-[140px] rounded-full object-cover shadow"
        />

        <img
          src="/images/d4853d90-1b8e-46a0-853e-638dff79a9b7.jpg"
          class="w-[140px] h-[140px] rounded-full object-cover shadow"
        />

        <img
          src="/images/mdb-master-esp32s3_assembled.jpeg"
          class="w-[140px] h-[140px] rounded-full object-cover shadow"
        />

        <img
          src="/images/mdb-slave-esp32s3_pcb_v2.jpg"
          class="w-[140px] h-[140px] rounded-full object-cover shadow"
        />

      </div>

    </section>

    <!-- FEATURES -->
    <section>

      <div class="grid md:grid-cols-2 lg:grid-cols-3 gap-8">

        <div class="bg-white p-6 rounded-xl shadow">
          <h3 class="font-semibold mb-2">
            MDB Protocol
          </h3>
          <p class="text-gray-600 text-sm">
            Full implementation of the MDB protocol used by vending machines.
          </p>
        </div>

        <div class="bg-white p-6 rounded-xl shadow">
          <h3 class="font-semibold mb-2">
            ESP32 Platform
          </h3>
          <p class="text-gray-600 text-sm">
            Built on ESP32 providing Wi-Fi, BLE and IoT capabilities.
          </p>
        </div>

        <div class="bg-white p-6 rounded-xl shadow">
          <h3 class="font-semibold mb-2">
            Cashless Payments
          </h3>
          <p class="text-gray-600 text-sm">
            Enables modern payment systems for vending machines.
          </p>
        </div>

        <div class="bg-white p-6 rounded-xl shadow">
          <h3 class="font-semibold mb-2">
            KiCad Hardware
          </h3>
          <p class="text-gray-600 text-sm">
            Open PCB design allowing customization and manufacturing.
          </p>
        </div>

        <div class="bg-white p-6 rounded-xl shadow">
          <h3 class="font-semibold mb-2">
            EVA DTS
          </h3>
          <p class="text-gray-600 text-sm">
            Support for telemetry and vending machine monitoring.
          </p>
        </div>

        <div class="bg-white p-6 rounded-xl shadow">
          <h3 class="font-semibold mb-2">
            Open Source
          </h3>
          <p class="text-gray-600 text-sm">
            Community driven project welcoming contributions.
          </p>
        </div>

      </div>

    </section>

    <!-- CONTRIBUTIONS -->
    <section class="text-center">

      <h2 class="text-2xl font-semibold mb-6">
        Contribute
      </h2>

      <p class="text-gray-600 max-w-2xl mx-auto mb-8">
        Contributions are welcome. Open issues, submit pull requests
        or help improve the documentation.
      </p>

      <div class="flex justify-center gap-4 mb-10">

        <img
          src="/images/pull-shark-default-498c279a747d.png"
          class="h-[70px]"
        />

        <img
          src="/images/starstruck-default-b6610abad518.png"
          class="h-[70px]"
        />

      </div>

      <a
        href="https://github.com/nodestark/mdb-esp32-cashless"
        target="_blank"
        class="inline-block px-8 py-3 bg-slate-900 text-white rounded-xl shadow hover:bg-slate-800 transition"
      >
        Visit GitHub Repository
      </a>

    </section>

  </div>
</template>