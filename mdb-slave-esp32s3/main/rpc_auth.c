#include "rpc_auth.h"

#include <string.h>
#include <stdio.h>
#include <mbedtls/md.h>

/* Device passkey, by reference (the owner keeps the buffer alive). */
static const char *s_key = "";

void rpc_auth_set_key(const char *passkey) {
	s_key = passkey ? passkey : "";
}

void calculate_hmac(const char *payload, size_t payload_len, unsigned char *output_hmac) {
	mbedtls_md_context_t ctx;
	mbedtls_md_init(&ctx);

	mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
	mbedtls_md_hmac_starts(&ctx, (const unsigned char *) s_key, strlen(s_key));
	mbedtls_md_hmac_update(&ctx, (const unsigned char *) payload, payload_len);
	mbedtls_md_hmac_finish(&ctx, output_hmac);

	mbedtls_md_free(&ctx);
}

bool rpc_verify_hmac(const char *msg, size_t msg_len, const char *sig_hex) {
	unsigned char hmac[32];
	calculate_hmac(msg, msg_len, hmac);

	char hex[65];
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", hmac[i]);

	return strcmp(hex, sig_hex) == 0;
}

void rpc_sign_text(const char *msg, char *out, size_t out_sz) {
	unsigned char hmac[32];
	calculate_hmac(msg, strlen(msg), hmac);

	char hex[65];
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", hmac[i]);

	snprintf(out, out_sz, "%s:%s", msg, hex);
}
