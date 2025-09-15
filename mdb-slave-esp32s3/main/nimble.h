#ifndef NIMBLE_H
#define NIMBLE_H

void sendBleNotification(char *notification, int notification_length);
void startBle(char *deviceName, void* writeBleCharacteristic_);
void renameBleDevice(char *deviceName);


#endif



