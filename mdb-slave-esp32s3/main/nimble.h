#ifndef NIMBLE_H
#define NIMBLE_H

void ble_notify_send(char *notification, int notification_length);
void ble_init(char *deviceName, void* writeBleCharacteristic_);
void ble_set_device_name(char *deviceName);

void ble_scan_start(int duration_seconds);
void ble_scan_stop(void);

#endif