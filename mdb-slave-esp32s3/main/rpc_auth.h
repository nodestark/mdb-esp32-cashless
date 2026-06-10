/*
 * rpc_auth — HMAC-SHA256 signing and verification for the VMflow control plane.
 *
 * Every RPC command and outbound telemetry line is authenticated with
 * HMAC-SHA256 over the device passkey. Keeping this in one module isolates the
 * money-path cryptography and lets it be unit-tested off-target.
 */
#ifndef RPC_AUTH_H
#define RPC_AUTH_H

#include <stddef.h>
#include <stdbool.h>

/* Point the HMAC key at the device passkey buffer. Kept by reference, so later
 * in-place updates to the buffer take effect. Call once at boot. */
void rpc_auth_set_key(const char *passkey);

/* HMAC-SHA256(passkey, payload[0..payload_len)) -> output_hmac (32 bytes). */
void calculate_hmac(const char *payload, size_t payload_len, unsigned char *output_hmac);

/* True if sig_hex equals the lowercase-hex HMAC of msg[0..msg_len). */
bool rpc_verify_hmac(const char *msg, size_t msg_len, const char *sig_hex);

/* Write "<msg>:<hmac_hex>" into out (NUL-terminated, truncated to out_sz). */
void rpc_sign_text(const char *msg, char *out, size_t out_sz);

#endif /* RPC_AUTH_H */
