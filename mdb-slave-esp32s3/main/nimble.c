#include <assert.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "bleprph.h"

// Variáveis globais
static uint8_t own_addr_type;
bool notify_state;
uint16_t notification_handle;
uint16_t conn_handle;
static bool ble_initialized = false;
static TaskHandle_t ble_host_task_handle = NULL;

// UUIDs
static const ble_uuid128_t gatt_svr_svc_uuid = BLE_UUID128_INIT(0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8, 0xed, 0x11, 0xda, 0x46, 0x42, 0xc6, 0xbb, 0xb2);
static const ble_uuid128_t gatt_svr_chr_uuid = BLE_UUID128_INIT(0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8, 0xed, 0x11, 0xde, 0x46, 0x76, 0x9c, 0xaf, 0xc9);

// Buffer para dados da característica
char characteristic_tosend_value[50] = "I am characteristic value";
char characteristic_received_value[500];

// Callback externo
void (*writeBleCharacteristic)(char*);

static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

// Funções auxiliares
static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len, void *dst, uint16_t *len) {
    uint16_t om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    return ble_hs_mbuf_to_flat(om, dst, max_len, len);
}

// Callback de acesso à característica
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {

    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        rc = os_mbuf_append(ctxt->om, characteristic_tosend_value, sizeof(characteristic_tosend_value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:

        rc = gatt_svr_chr_write(ctxt->om, 1, sizeof(characteristic_received_value), characteristic_received_value, NULL);

        writeBleCharacteristic( (char*) &characteristic_received_value );

        return rc;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// Definição do serviço e característica
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .val_handle = &notification_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        }
    },
    { 0 }
};

// Task principal do host BLE
void bleprph_host_task(void *param) {
    ble_host_task_handle = xTaskGetCurrentTaskHandle();
    nimble_port_run();
    nimble_port_freertos_deinit();
    ble_host_task_handle = NULL;
    vTaskDelete(NULL);
}

// Advertising
static void bleprph_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t*)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event, NULL);
}

// Callback de sincronização
static void bleprph_on_sync(void) {
    ble_hs_id_infer_auto(0, &own_addr_type);
    bleprph_advertise();
}

// Inicialização do BLE
void startBleDevice(char *deviceName, void* writeBleCharacteristic_) { //! Call this function to start BLE
    writeBleCharacteristic = writeBleCharacteristic_;

    nimble_port_init();
    ble_hs_cfg.sync_cb = bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;

    // Configurações de segurança simplificadas
    ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;
    ble_hs_cfg.sm_sc = 0;

    gatt_svr_init();
    ble_svc_gap_device_name_set(deviceName);

    nimble_port_freertos_init(bleprph_host_task);
    ble_initialized = true;
}

void renameBleDevice(char *deviceName) {
    if (!ble_initialized) {
        return; // BLE não iniciado ainda
    }

    ble_svc_gap_device_name_set(deviceName);

    // Opcional: reiniciar advertising para refletir o novo nome
    ble_gap_adv_stop();   // para advertising atual
    bleprph_advertise();  // inicia advertising novamente com o novo nome
}

// Callback de registro GATT
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    // Log removido para simplificação
}

// Inicialização dos serviços GATT
int gatt_svr_init(void) {
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) return rc;

    return ble_gatts_add_svcs(gatt_svr_svcs);
}

// Eventos GAP
static int bleprph_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            bleprph_advertise();
        }
        conn_handle = event->connect.conn_handle;
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        bleprph_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == notification_handle) {
            notify_state = event->subscribe.cur_notify;
        }
        break;

    default:
        break;
    }

    return 0;
}

// Envio de notificações
void sendBleNotification(char *notification, int notification_length) {
    if (!notify_state || !ble_initialized || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(notification, notification_length);
    ble_gattc_notify_custom(conn_handle, notification_handle, om);
}
