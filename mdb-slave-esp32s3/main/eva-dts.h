#pragma once

// EVA-DTS DEX / DDCMP audit telemetry over UART1.

// Configure UART1 and allocate the DEX ring buffer. Call once at startup.
void telemetry_init(void);

// Read DDCMP + DEX audit data from the VMC and publish it to
// domain.vmflow.xyz/<subdomain>/rpc/dex. Signature matches an esp_timer
// callback (arg is unused), so it can also be called directly.
void request_telemetry_data(void *arg);
