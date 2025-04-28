#include <assert.h>
#include <stdio.h>
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
#include "nimble.h"

//@______________________Declare some variables____________________________
static const char *tag = "NimBLE_BLE";
static uint8_t own_addr_type;
bool notify_state; //!! When client subscribe to notifications, the value is set to 1.Check this value before sending notifictions.
uint16_t notification_handle;
uint16_t conn_handle;

//@_____________________Define UUIDs______________________________________
//!! b2bbc642-46da-11ed-b878-0242ac120002
static const ble_uuid128_t gatt_svr_svc_uuid = BLE_UUID128_INIT(0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8, 0xed, 0x11, 0xda, 0x46, 0x42, 0xc6, 0xbb, 0xb2);

//!! c9af9c76-46de-11ed-b878-0242ac120002
static const ble_uuid128_t gatt_svr_chr_uuid = BLE_UUID128_INIT(0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8, 0xed, 0x11, 0xde, 0x46, 0x76, 0x9c, 0xaf, 0xc9);

//@_____Some variables used in service and characteristic declaration______

char characteristic_tosend_value[50] = "I am characteristic value"; //!! When client read characteristic, he get this value. You can also set this value in your code.
char characteristic_received_value[500]; //!! When client write to characteristic , he set value of this. You can read it in code.

static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

void print_addr(const void *addr) {

    const uint8_t *u8p;

    u8p = addr;
    MODLOG_DFLT(INFO, "%02x:%02x:%02x:%02x:%02x:%02x", u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

//@___________________________Heart of nimble code _________________________________________
static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len, void *dst, uint16_t *len) {

	uint16_t om_len;
	int rc;

	om_len = OS_MBUF_PKTLEN(om);
	if (om_len < min_len || om_len > max_len) {
		return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
	}

	rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
	if (rc != 0) {
		return BLE_ATT_ERR_UNLIKELY;
	}

	return 0;
}

void (*writeBleCharacteristic)(char*);

//!! Callback function. When ever characrstic will be accessed by user, this function will execute
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {

	int rc;

	switch (ctxt->op) {
	case BLE_GATT_ACCESS_OP_READ_CHR: //!! In case user accessed this characterstic to read its value, bellow lines will execute
		rc = os_mbuf_append(ctxt->om, &characteristic_tosend_value, sizeof(characteristic_tosend_value) );
		return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

	case BLE_GATT_ACCESS_OP_WRITE_CHR: //!! In case user accessed this characterstic to write, bellow lines will executed.
		rc = gatt_svr_chr_write(ctxt->om, 1, sizeof(characteristic_received_value), &characteristic_received_value, NULL); //!! Function "gatt_svr_chr_write" will fire.
		//! Use received value in you code. For example

		writeBleCharacteristic( (char*) &characteristic_received_value );

		return rc;
	default:
		assert(0);
		return BLE_ATT_ERR_UNLIKELY;
	}
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = { {
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = &gatt_svr_svc_uuid.u,
		.characteristics = (struct ble_gatt_chr_def[] ) { {
				.uuid = &gatt_svr_chr_uuid.u,     //!! UUID as given above
				.access_cb = gatt_svr_chr_access, //!! Callback function. When ever this characrstic will be accessed by user, this function will execute
				.val_handle = &notification_handle,
				.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY, //!! flags set permissions. In this case User can read this characterstic, can write to it,and get notified.
			}, { 0, /* No more characteristics in this service. This is necessary */
			} } , },
		{ 0, /* No more services. This is necessary */
}, };

void bleprph_host_task(void *param) {
	ESP_LOGI(tag, "BLE Host Task Started");
	/* This function will return only when nimble_port_stop() is executed */
	nimble_port_run();

	nimble_port_freertos_deinit();
}

static void bleprph_advertise(void) {
	struct ble_gap_adv_params adv_params;
	struct ble_hs_adv_fields fields;
	const char *name;

	/**
	 *  Set the advertisement data included in our advertisements:
	 *     o Flags (indicates advertisement type and other general info).
	 *     o Advertising tx power.
	 *     o Device name.
	 *     o 16-bit service UUIDs (alert notifications).
	 */

	memset(&fields, 0, sizeof( fields) );

	/* Advertise two flags:
	 *     o Discoverability in forthcoming advertisement (general)
	 *     o BLE-only (BR/EDR unsupported).
	 */
	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

	/* Indicate that the TX power level field should be included; have the
	 * stack fill this value automatically.  This is done by assigning the
	 * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
	 */
	fields.tx_pwr_lvl_is_present = 1;
	fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

	name = ble_svc_gap_device_name();
	fields.name = (uint8_t*) name;
	fields.name_len = strlen(name);
	fields.name_is_complete = 1;

	fields.uuids16 = (ble_uuid16_t[] ) { BLE_UUID16_INIT( GATT_SVR_SVC_ALERT_UUID) };
	fields.num_uuids16 = 1;
	fields.uuids16_is_complete = 1;

	int rc;

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
		return;
	}

	/* Begin advertising. */
	memset(&adv_params, 0, sizeof( adv_params) );
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
	rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event, NULL);
	if (rc != 0) {
		MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
		return;
	}
}

static void bleprph_on_sync(void) {

	int rc;

	rc = ble_hs_util_ensure_addr(0);
	assert(rc == 0);

	/* Figure out address to use while advertising (no privacy for now) */
	rc = ble_hs_id_infer_auto(0, &own_addr_type);
	if (rc != 0) {
		MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
		return;
	}

	/* Printing ADDR */
	uint8_t addr_val[6] = { 0 };
	rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

	MODLOG_DFLT(INFO, "Device Address: ");
	print_addr(addr_val);
	MODLOG_DFLT(INFO, "\n");
	/* Begin advertising. */
	bleprph_advertise();
}

void startBle(char *deviceName, void* writeBleCharacteristic_) { //! Call this function to start BLE

	//! Below is the sequence of APIs to be called to init/enable NimBLE host and ESP controller:
	printf("\n Staring BLE \n");

	writeBleCharacteristic= writeBleCharacteristic_;

	nimble_port_init();
	/* Initialize the NimBLE host configuration. */
	ble_hs_cfg.reset_cb = (void*) 0;
	ble_hs_cfg.sync_cb = bleprph_on_sync;
	ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;
#ifdef CONFIG_EXAMPLE_BONDING
  ble_hs_cfg.sm_bonding = 1;
#endif
#ifdef CONFIG_EXAMPLE_MITM
  ble_hs_cfg.sm_mitm = 1;
#endif
#ifdef CONFIG_EXAMPLE_USE_SC
  ble_hs_cfg.sm_sc = 1;
#else
	ble_hs_cfg.sm_sc = 0;
#endif
#ifdef CONFIG_EXAMPLE_BONDING
  ble_hs_cfg.sm_our_key_dist = 1;
  ble_hs_cfg.sm_their_key_dist = 1;
#endif

	int rc;

	rc = gatt_svr_init();
	assert(rc == 0);

	/* Set the default device name. */
	rc = ble_svc_gap_device_name_set(deviceName); //!! Set the name of this device
	assert(rc == 0);

	/* XXX Need to have template for store */

	nimble_port_freertos_init(bleprph_host_task);
	printf("characteristic_value at end of startBLE=%s\n", characteristic_tosend_value);
}

//@________________Bellow code will remain as it is.Take it abracadabra of BLE 😀 😀________________

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {

	char buf[BLE_UUID_STR_LEN];

	switch (ctxt->op) {
	case BLE_GATT_REGISTER_OP_SVC:
		MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
		break;

	case BLE_GATT_REGISTER_OP_CHR:
		MODLOG_DFLT(DEBUG, "registering characteristic %s with " "def_handle=%d val_handle=%d\n", ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
		break;

	case BLE_GATT_REGISTER_OP_DSC:
		MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n", ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
		break;

	default:
		assert(0);
		break;
	}
}

int gatt_svr_init(void) {

	ble_svc_gap_init();
	ble_svc_gatt_init();

	int rc = ble_gatts_count_cfg(gatt_svr_svcs);
	if (rc != 0) {
		return rc;
	}

	rc = ble_gatts_add_svcs(gatt_svr_svcs);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

static void bleprph_print_conn_desc(struct ble_gap_conn_desc *desc) {
	MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=", desc->conn_handle, desc->our_ota_addr.type);
	print_addr(desc->our_ota_addr.val);
	MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=", desc->our_id_addr.type);
	print_addr(desc->our_id_addr.val);
	MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=", desc->peer_ota_addr.type);
	print_addr(desc->peer_ota_addr.val);
	MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=", desc->peer_id_addr.type);
	print_addr(desc->peer_id_addr.val);
	MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
			"encrypted=%d authenticated=%d bonded=%d\n", desc->conn_itvl,
			desc->conn_latency, desc->supervision_timeout,
			desc->sec_state.encrypted, desc->sec_state.authenticated,
			desc->sec_state.bonded);
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int bleprph_gap_event(struct ble_gap_event *event, void *arg) {
	struct ble_gap_conn_desc desc;
	int rc;

	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		/* A new connection was established or a connection attempt failed. */
		MODLOG_DFLT(INFO, "connection %s; status=%d ", event->connect.status == 0 ? "established" : "failed", event->connect.status);
		if (event->connect.status == 0) {
			rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
			assert(rc == 0);
			bleprph_print_conn_desc(&desc);
		}
		MODLOG_DFLT(INFO, "\n");

		if (event->connect.status != 0) {
			/* Connection failed; resume advertising. */
			bleprph_advertise();
		}
		conn_handle = event->connect.conn_handle;
		return 0;

	case BLE_GAP_EVENT_DISCONNECT:
		MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
		bleprph_print_conn_desc(&event->disconnect.conn);
		MODLOG_DFLT(INFO, "\n");

		/* Connection terminated; resume advertising. */
		bleprph_advertise();
		return 0;

	case BLE_GAP_EVENT_CONN_UPDATE:
		/* The central has updated the connection parameters. */
		MODLOG_DFLT(INFO, "connection updated; status=%d ", event->conn_update.status);
		rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
		assert(rc == 0);
		bleprph_print_conn_desc(&desc);
		MODLOG_DFLT(INFO, "\n");
		return 0;

	case BLE_GAP_EVENT_ADV_COMPLETE:
		MODLOG_DFLT(INFO, "advertise complete; reason=%d", event->adv_complete.reason);
		bleprph_advertise();
		return 0;

	case BLE_GAP_EVENT_SUBSCRIBE:

		MODLOG_DFLT(INFO, "subscribe event; cur_notify=%d\n value handle; "
				"val_handle=%d\n"
				"conn_handle=%d attr_handle=%d "
				"reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
				event->subscribe.conn_handle, event->subscribe.attr_handle,
				event->subscribe.reason, event->subscribe.prev_notify,
				event->subscribe.cur_notify, event->subscribe.cur_notify,
				notification_handle, //!! Client Subscribed to notification_handle
				event->subscribe.prev_indicate, event->subscribe.cur_indicate);

		if (event->subscribe.attr_handle == notification_handle) {
			printf("\nSubscribed with notification_handle =%d\n", event->subscribe.attr_handle);
			notify_state = event->subscribe.cur_notify; //!! As the client is now subscribed to notifications, the value is set to 1
			printf("notify_state=%d\n", notify_state);
		}

		return 0;

	case BLE_GAP_EVENT_MTU:
		MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n", event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
		return 0;
	}

	return 0;
}

void sendBleNotification(char *notification, int notification_length) {
	//!Use this function to send notification once (after setting value of variable "notification")

	if (notify_state) { //!! This value is checked so that we don't send notifications if user has not subscribed to our notification handle.

		struct os_mbuf *om= ble_hs_mbuf_from_flat(notification, notification_length); //! Value of variable "notification" will be sent as notification.

		int rc = ble_gattc_notify_custom(conn_handle, notification_handle, om);
		if (rc != 0) {
			printf("\n error notifying; rc\n");
		}
	} else {
		printf("user not subscribed to notifications.\n");
	}
}
