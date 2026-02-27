#ifndef WEBUI_SERVER_H
#define WEBUI_SERVER_H

#define AP_SSID "VMflow"
#define AP_PASS "12345678"

void start_softap(void);
void start_rest_server();
void stop_rest_server();

void start_dns_server(void);
void stop_dns_server(void);

#endif