#ifndef NIMBLE_H
#define NIMBLE_H

#define PAX_REPORT_INTERVAL_SEC     (60*60)         // 1 hora
#define PAX_SCAN_DURATION_SEC       (8)             // 8 segundos
#define PAX_SCAN_INTERVAL_US        (5*60*1000000)  // 5 minutos

void ble_notify_send(char *notification, int notification_length);
void ble_init(char *deviceName, void* ble_event_handler_, void* ble_pax_event_handler_);
void ble_set_device_name(char *deviceName);

void ble_scan_start(int duration_seconds);
void ble_scan_stop(void);

#endif