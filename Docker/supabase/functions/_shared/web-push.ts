/**
 * Web Push utility for Supabase Edge Functions (Deno).
 *
 * Implements:
 *  - VAPID (RFC 8292) — JWT signed with ECDSA P-256
 *  - Web Push Encryption (RFC 8291) — aes128gcm content encoding
 *
 * Uses only Deno's built-in Web Crypto API — zero npm dependencies.
 */

import { createClient, SupabaseClient } from 'jsr:@supabase/supabase-js@2'

// ─── Types ──────────────────────────────────────────────────────────────────

interface PushSubscription {
  id: string
  endpoint: string
  p256dh: string
  auth: string
}

interface VapidConfig {
  publicKey: string   // base64url-encoded 65-byte uncompressed public key
  privateKey: string  // base64url-encoded 32-byte raw private key
  subject: string     // mailto: or https: URI
}

interface PushPayload {
  title: string
  body: string
  data?: Record<string, unknown>
}

// ─── Base64url helpers ──────────────────────────────────────────────────────

function base64urlToUint8Array(b64url: string): Uint8Array {
  const padding = '='.repeat((4 - b64url.length % 4) % 4)
  const b64 = (b64url + padding).replace(/-/g, '+').replace(/_/g, '/')
  const raw = atob(b64)
  return Uint8Array.from(raw, c => c.charCodeAt(0))
}

function uint8ArrayToBase64url(bytes: Uint8Array): string {
  let binary = ''
  for (const b of bytes) binary += String.fromCharCode(b)
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

// ─── VAPID JWT (RFC 8292) ───────────────────────────────────────────────────

async function createVapidJwt(
  audience: string,
  subject: string,
  privateKeyRaw: Uint8Array,
  publicKeyRaw: Uint8Array, // 65-byte uncompressed P-256 public key
): Promise<string> {
  const header = { typ: 'JWT', alg: 'ES256' }
  const now = Math.floor(Date.now() / 1000)
  const payload = { aud: audience, exp: now + 12 * 3600, sub: subject }

  const headerB64 = uint8ArrayToBase64url(new TextEncoder().encode(JSON.stringify(header)))
  const payloadB64 = uint8ArrayToBase64url(new TextEncoder().encode(JSON.stringify(payload)))
  const signingInput = new TextEncoder().encode(`${headerB64}.${payloadB64}`)

  // Import private key via JWK using the public key x/y coordinates
  // publicKeyRaw is 65 bytes: 0x04 || x (32 bytes) || y (32 bytes)
  const x = uint8ArrayToBase64url(publicKeyRaw.subarray(1, 33))
  const y = uint8ArrayToBase64url(publicKeyRaw.subarray(33, 65))
  const d = uint8ArrayToBase64url(privateKeyRaw)

  const key = await crypto.subtle.importKey(
    'jwk',
    { kty: 'EC', crv: 'P-256', x, y, d },
    { name: 'ECDSA', namedCurve: 'P-256' },
    false,
    ['sign'],
  )

  const signature = await crypto.subtle.sign(
    { name: 'ECDSA', hash: 'SHA-256' },
    key,
    signingInput,
  )

  // Convert DER signature to raw r||s (each 32 bytes)
  const sigBytes = new Uint8Array(signature)
  const rawSig = derToRaw(sigBytes)

  return `${headerB64}.${payloadB64}.${uint8ArrayToBase64url(rawSig)}`
}

function derToRaw(der: Uint8Array): Uint8Array {
  // If already 64 bytes (raw format), return as-is
  if (der.length === 64) return der

  // Parse DER SEQUENCE { INTEGER r, INTEGER s }
  const raw = new Uint8Array(64)
  let offset = 0

  if (der[offset] === 0x30) {
    offset++ // SEQUENCE tag
    if (der[offset] & 0x80) offset += (der[offset] & 0x7f) + 1
    else offset++ // length
  }

  // Parse r
  if (der[offset] === 0x02) {
    offset++ // INTEGER tag
    const rLen = der[offset++]
    const rStart = offset + (rLen > 32 ? rLen - 32 : 0)
    const rDest = 32 - Math.min(rLen, 32)
    raw.set(der.subarray(rStart, offset + rLen), rDest)
    offset += rLen
  }

  // Parse s
  if (der[offset] === 0x02) {
    offset++ // INTEGER tag
    const sLen = der[offset++]
    const sStart = offset + (sLen > 32 ? sLen - 32 : 0)
    const sDest = 32 + 32 - Math.min(sLen, 32)
    raw.set(der.subarray(sStart, offset + sLen), sDest)
  }

  return raw
}

// ─── Web Push Encryption (RFC 8291 aes128gcm) ──────────────────────────────

async function encryptPayload(
  plaintext: Uint8Array,
  subscriptionPubKey: Uint8Array, // 65-byte uncompressed P-256 public key
  authSecret: Uint8Array,         // 16-byte auth secret
): Promise<{ body: Uint8Array; localPublicKey: Uint8Array }> {
  // 1. Generate ephemeral ECDH key pair
  const localKeyPair = await crypto.subtle.generateKey(
    { name: 'ECDH', namedCurve: 'P-256' },
    true,
    ['deriveBits'],
  )

  const localPublicKeyRaw = new Uint8Array(
    await crypto.subtle.exportKey('raw', localKeyPair.publicKey),
  )

  // 2. Import subscriber's public key
  const subscriberKey = await crypto.subtle.importKey(
    'raw',
    subscriptionPubKey,
    { name: 'ECDH', namedCurve: 'P-256' },
    false,
    [],
  )

  // 3. ECDH shared secret
  const sharedSecretBits = await crypto.subtle.deriveBits(
    { name: 'ECDH', public: subscriberKey },
    localKeyPair.privateKey,
    256,
  )
  const sharedSecret = new Uint8Array(sharedSecretBits)

  // 4. Key derivation per RFC 8291
  // IKM = HKDF-SHA256(sharedSecret, authSecret, "WebPush: info\0" + subscriberPub + localPub, 32)
  const infoPrefix = new TextEncoder().encode('WebPush: info\0')
  const ikm_info = new Uint8Array(infoPrefix.length + 65 + 65)
  ikm_info.set(infoPrefix)
  ikm_info.set(subscriptionPubKey, infoPrefix.length)
  ikm_info.set(localPublicKeyRaw, infoPrefix.length + 65)

  const ikm = await hkdf(sharedSecret, authSecret, ikm_info, 32)

  // 5. Generate random 16-byte salt
  const salt = crypto.getRandomValues(new Uint8Array(16))

  // 6. Derive content encryption key (CEK) and nonce
  const cekInfo = new TextEncoder().encode('Content-Encoding: aes128gcm\0')
  const nonceInfo = new TextEncoder().encode('Content-Encoding: nonce\0')

  const cek = await hkdf(ikm, salt, cekInfo, 16)
  const nonce = await hkdf(ikm, salt, nonceInfo, 12)

  // 7. Pad plaintext (add delimiter byte 0x02 + optional padding)
  const padded = new Uint8Array(plaintext.length + 1)
  padded.set(plaintext)
  padded[plaintext.length] = 0x02 // delimiter

  // 8. Encrypt with AES-128-GCM
  const cryptoKey = await crypto.subtle.importKey(
    'raw',
    cek,
    { name: 'AES-GCM' },
    false,
    ['encrypt'],
  )

  const ciphertext = new Uint8Array(
    await crypto.subtle.encrypt(
      { name: 'AES-GCM', iv: nonce },
      cryptoKey,
      padded,
    ),
  )

  // 9. Build binary body: salt(16) + rs(4) + idlen(1) + keyid(65) + ciphertext
  const rs = 4096
  const header = new Uint8Array(16 + 4 + 1 + 65)
  header.set(salt) // salt
  header[16] = (rs >> 24) & 0xff
  header[17] = (rs >> 16) & 0xff
  header[18] = (rs >> 8) & 0xff
  header[19] = rs & 0xff
  header[20] = 65 // idlen
  header.set(localPublicKeyRaw, 21) // keyid = local public key

  const body = new Uint8Array(header.length + ciphertext.length)
  body.set(header)
  body.set(ciphertext, header.length)

  return { body, localPublicKey: localPublicKeyRaw }
}

async function hkdf(
  ikm: Uint8Array,
  salt: Uint8Array,
  info: Uint8Array,
  length: number,
): Promise<Uint8Array> {
  const key = await crypto.subtle.importKey('raw', ikm, 'HKDF', false, ['deriveBits'])
  const bits = await crypto.subtle.deriveBits(
    { name: 'HKDF', hash: 'SHA-256', salt, info },
    key,
    length * 8,
  )
  return new Uint8Array(bits)
}

// ─── Send Push Notification ─────────────────────────────────────────────────

async function sendPushNotification(
  subscription: { endpoint: string; p256dh: string; auth: string },
  payload: PushPayload,
  vapid: VapidConfig,
): Promise<Response> {
  const payloadBytes = new TextEncoder().encode(JSON.stringify(payload))
  const subscriberPubKey = base64urlToUint8Array(subscription.p256dh)
  const authSecret = base64urlToUint8Array(subscription.auth)
  const vapidPrivateKey = base64urlToUint8Array(vapid.privateKey)
  const vapidPublicKey = base64urlToUint8Array(vapid.publicKey)

  // Encrypt payload
  const { body } = await encryptPayload(payloadBytes, subscriberPubKey, authSecret)

  // Build VAPID Authorization header
  const endpointUrl = new URL(subscription.endpoint)
  const audience = `${endpointUrl.protocol}//${endpointUrl.host}`
  const jwt = await createVapidJwt(audience, vapid.subject, vapidPrivateKey, vapidPublicKey)

  return fetch(subscription.endpoint, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/octet-stream',
      'Content-Encoding': 'aes128gcm',
      'Content-Length': String(body.length),
      Authorization: `vapid t=${jwt}, k=${vapid.publicKey}`,
      TTL: '86400',
      Urgency: 'normal',
    },
    body,
  })
}

// ─── Main helper: send to all users in a company ────────────────────────────

export async function sendPushToUsers(
  adminClient: SupabaseClient,
  companyId: string,
  notificationType: string,
  payload: PushPayload,
): Promise<{ sent: number; expired: number }> {
  // Graceful degradation: skip if VAPID keys are not configured
  const publicKey = Deno.env.get('VAPID_PUBLIC_KEY')
  const privateKey = Deno.env.get('VAPID_PRIVATE_KEY')
  const subject = Deno.env.get('VAPID_SUBJECT')

  if (!publicKey || !privateKey || !subject) {
    return { sent: 0, expired: 0 }
  }

  const vapid: VapidConfig = { publicKey, privateKey, subject }

  // Query subscriptions for users in this company who want this notification type.
  // Absence of a preference row = enabled (opt-in by default).
  const { data: allSubs, error: subsError } = await adminClient
    .from('push_subscriptions')
    .select('id, endpoint, p256dh, auth, user_id')

  if (subsError || !allSubs || allSubs.length === 0) {
    return { sent: 0, expired: 0 }
  }

  // Filter by company membership
  const { data: members } = await adminClient
    .from('organization_members')
    .select('user_id')
    .eq('company_id', companyId)

  if (!members || members.length === 0) {
    return { sent: 0, expired: 0 }
  }

  const memberIds = new Set(members.map((m: { user_id: string }) => m.user_id))

  // Check preferences (absence = enabled)
  const { data: disabledPrefs } = await adminClient
    .from('notification_preferences')
    .select('user_id')
    .eq('notification_type', notificationType)
    .eq('enabled', false)

  const disabledUserIds = new Set((disabledPrefs ?? []).map((p: { user_id: string }) => p.user_id))

  const subscriptions = allSubs.filter(
    (s: { user_id: string }) => memberIds.has(s.user_id) && !disabledUserIds.has(s.user_id),
  )

  if (subscriptions.length === 0) {
    return { sent: 0, expired: 0 }
  }

  let sent = 0
  let expired = 0
  const expiredIds: string[] = []

  // Send push to each subscription in parallel
  await Promise.allSettled(
    subscriptions.map(async (sub: PushSubscription) => {
      try {
        const response = await sendPushNotification(sub, payload, vapid)
        if (response.ok || response.status === 201) {
          sent++
        } else if (response.status === 404 || response.status === 410) {
          // Subscription expired or invalid — mark for deletion
          expired++
          expiredIds.push(sub.id)
        } else {
          console.warn(`Push failed for ${sub.endpoint}: ${response.status}`)
        }
      } catch (err) {
        console.warn(`Push error for ${sub.endpoint}:`, err)
      }
    }),
  )

  // Clean up expired subscriptions
  if (expiredIds.length > 0) {
    try {
      await adminClient
        .from('push_subscriptions')
        .delete()
        .in('id', expiredIds)
    } catch (err) {
      console.warn('Failed to clean up expired subscriptions:', err)
    }
  }

  return { sent, expired }
}
