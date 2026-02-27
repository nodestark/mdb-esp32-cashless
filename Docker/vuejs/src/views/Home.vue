<template>
  <div class="max-w-5xl mx-auto mt-16 px-6 leading-relaxed">

    <!-- Hero Image -->
    <img
      src="/images/Flux_Schnell_A_futuristic_vending_machine_with_bright_yellow_n_2.png"
      class="w-full max-h-[420px] object-cover rounded-2xl shadow-lg mb-10"
    />

    <!-- Title -->
    <h1 class="text-3xl md:text-4xl font-bold mb-6">
      MDB Protocol Implementation for Cashless Vending Machine
    </h1>

    <!-- Description -->
    <p class="text-gray-700 text-lg mb-10">
      This is an open source project dedicated to implementing the MDB (Multi-Drop Bus) protocol for cashless vending machines, using ESP32 as the main platform and KiCad for PCB design.
      The MDB protocol is widely used in vending machines for communication between devices, enabling integration with cashless payment systems.
    </p>

    <!-- Horizontal Circular Images -->
    <div class="flex flex-wrap justify-center mt-6">
      <img
        src="/images/1411051686640.jpg"
        class="w-[150px] h-[150px] rounded-full object-cover m-2 shadow"
      />
      <img
        src="/images/d4853d90-1b8e-46a0-853e-638dff79a9b7.jpg"
        class="w-[150px] h-[150px] rounded-full object-cover m-2 shadow"
      />
      <img
        src="/images/mdb-master-esp32s3_assembled.jpeg"
        class="w-[150px] h-[150px] rounded-full object-cover m-2 shadow"
      />
      <img
        src="/images/mdb-slave-esp32s3_pcb_v2.jpg"
        class="w-[150px] h-[150px] rounded-full object-cover m-2 shadow"
      />
    </div>

    <!-- Features -->
    <h2 class="text-2xl font-semibold mb-4">
      Key Features:
    </h2>

    <ul class="list-disc list-inside space-y-2 text-gray-700 mb-12">
      <li>Implementation of the MDB protocol for communication with vending machines;</li>
      <li>Support for integrating cashless payment systems into vending machines;</li>
      <li>Utilization of ESP32 as the hardware platform, offering flexibility and advanced IoT features;</li>
      <li>PCB design done in KiCad, facilitating production and customization of the hardware;</li>
      <li>Support for EVA DTS (European Vending Association Data Transfer Standard), allowing remote monitoring and control of machines.</li>
    </ul>

    <!-- Contribute -->
    <h2 class="text-2xl font-semibold mb-4">
      How to Contribute:
    </h2>

    <div class="flex justify-center mb-4">
      <img
        src="/images/pull-shark-default-498c279a747d.png"
        class="h-[70px] m-2"
      />
      <img
        src="/images/starstruck-default-b6610abad518.png"
        class="h-[70px] m-2"
      />
    </div>

    <ul class="list-disc list-inside space-y-2 text-gray-700 mb-12">
      <li>Contributions are welcome! Feel free to open issues, send pull requests, or propose new features;</li>
      <li>Before submitting a pull request, make sure the code complies with the style and quality guidelines defined in the project;</li>
      <li>Help us improve documentation by adding usage examples, installation instructions, and any other relevant information.</li>
    </ul>

    <!-- Links -->
    <div class="space-y-6">

      <a
        href="https://github.com/nodestark/mdb-esp32-cashless"
        target="_blank"
        class="text-blue-600 hover:underline block text-lg"
      >
        View repository on GitHub
      </a>

      <p class="text-gray-700 text-lg">
        Talk to a specialist:
        <a
          href="https://t.me/vmflowxyz"
          target="_blank"
          class="text-blue-600 hover:underline ml-1"
        >
          @vmflowxyz on Telegram
        </a>
      </p>

      <RouterLink
        to="/dashboard"
        class="inline-block mt-6 px-6 py-3 bg-slate-800 text-white rounded-xl shadow-md hover:bg-slate-700 transition duration-200">
        Go to Dashboard
      </RouterLink>

    </div>

  </div>
</template>

<script>
export default {
  name: 'Home'
}
</script>