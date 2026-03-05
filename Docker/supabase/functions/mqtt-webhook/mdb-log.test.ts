/**
 * Tests for the mdb-log handler logic in mqtt-webhook.
 *
 * Run: deno test Docker/supabase/functions/mqtt-webhook/mdb-log.test.ts --allow-env
 *
 * These tests exercise the mdb-log processing logic by simulating
 * the Supabase admin client and verifying the correct DB operations.
 */

import { assertEquals, assertExists } from 'jsr:@std/assert'
import { encodeBase64 } from 'jsr:@std/encoding/base64'

// ── Helpers ───────────────────────────────────────────────────────────────────

/** Encode a plain-text JSON payload as base64 (matching forwarder behaviour). */
function encodePayload(obj: Record<string, unknown>): string {
  return encodeBase64(new TextEncoder().encode(JSON.stringify(obj)))
}

/** Build a minimal mock Supabase client that records calls. */
function createMockAdminClient(opts: {
  selectResult?: Record<string, unknown> | null
  selectError?: Error | null
  updateError?: Error | null
  insertError?: Error | null
}) {
  const calls: {
    selects: Array<{ table: string; filters: Record<string, unknown> }>
    updates: Array<{ table: string; data: Record<string, unknown>; filters: Record<string, unknown> }>
    inserts: Array<{ table: string; data: Record<string, unknown> }>
  } = { selects: [], updates: [], inserts: [] }

  const client = {
    from(table: string) {
      return {
        select(columns: string) {
          const filterState: Record<string, unknown> = {}
          return {
            eq(col: string, val: unknown) {
              filterState[col] = val
              return this
            },
            single() {
              calls.selects.push({ table, filters: { ...filterState } })
              if (opts.selectError) return Promise.resolve({ data: null, error: opts.selectError })
              return Promise.resolve({ data: opts.selectResult ?? null, error: null })
            },
          }
        },
        update(data: Record<string, unknown>) {
          const filterState: Record<string, unknown> = {}
          return {
            eq(col: string, val: unknown) {
              filterState[col] = val
              calls.updates.push({ table, data: { ...data }, filters: { ...filterState } })
              if (opts.updateError) return Promise.resolve({ error: opts.updateError })
              return Promise.resolve({ error: null })
            },
          }
        },
        insert(data: Record<string, unknown> | Record<string, unknown>[]) {
          calls.inserts.push({ table, data: Array.isArray(data) ? data[0] : data })
          if (opts.insertError) return Promise.resolve({ error: opts.insertError })
          return Promise.resolve({ error: null })
        },
      }
    },
  }

  return { client, calls }
}

// ── Extracted handler logic (mirrors mqtt-webhook/index.ts mdb-log block) ──

interface MdbLogResult {
  status: number
  body: Record<string, unknown>
  stateChanged: boolean
}

async function handleMdbLog(
  payloadB64: string,
  deviceId: string,
  adminClient: ReturnType<typeof createMockAdminClient>['client'],
): Promise<MdbLogResult> {
  // Decode base64 → parse JSON
  const { decodeBase64 } = await import('jsr:@std/encoding/base64')
  const logBytes = decodeBase64(payloadB64)
  const rawJson = new TextDecoder().decode(logBytes)

  let diag: Record<string, unknown>
  try {
    diag = JSON.parse(rawJson)
  } catch {
    return { status: 400, body: { error: 'invalid JSON in mdb-log payload' }, stateChanged: false }
  }

  const newState = diag.state as string | undefined
  if (!newState) {
    return { status: 400, body: { error: 'missing state field in mdb-log' }, stateChanged: false }
  }

  // Read current diagnostics
  const { data: deviceRow, error: fetchErr } = await adminClient
    .from('embeddeds')
    .select('mdb_diagnostics, company')
    .eq('id', deviceId)
    .single()

  if (fetchErr) throw fetchErr
  if (!deviceRow) {
    return { status: 404, body: { error: 'device not found' }, stateChanged: false }
  }

  const prevState = (deviceRow.mdb_diagnostics as Record<string, unknown> | null)?.state as string | undefined
  const stateChanged = prevState !== newState

  // Always update latest diagnostics
  const diagPayload = { ...diag, updated_at: new Date().toISOString() }

  const { error: updateErr } = await adminClient
    .from('embeddeds')
    .update({ mdb_diagnostics: diagPayload })
    .eq('id', deviceId)

  if (updateErr) throw updateErr

  // Insert history only on state change
  if (stateChanged) {
    const { error: insertErr } = await adminClient
      .from('mdb_log')
      .insert({
        embedded_id: deviceId,
        state: newState,
        prev_state: prevState ?? null,
        addr: (diag.addr as string) ?? null,
        polls: (diag.polls as number) ?? null,
        chk_err: (diag.chkErr as number) ?? null,
        last_cmd: (diag.lastCmd as string) ?? null,
        raw: diag,
      })

    if (insertErr) throw insertErr
  }

  return { status: 200, body: { ok: true, state_changed: stateChanged }, stateChanged }
}

// ── Tests ─────────────────────────────────────────────────────────────────────

const DEVICE_ID = '00000000-0000-0000-0000-000000000001'
const COMPANY_ID = '00000000-0000-0000-0000-000000000099'

Deno.test('mdb-log: valid payload updates embeddeds and creates history on state change', async () => {
  const payload = { state: 'ENABLED', addr: '0x10', polls: 1500, chkErr: 0, lastCmd: 'READER_ENABLE' }
  const { client, calls } = createMockAdminClient({
    selectResult: {
      mdb_diagnostics: { state: 'DISABLED' },
      company: COMPANY_ID,
    },
  })

  const result = await handleMdbLog(encodePayload(payload), DEVICE_ID, client)

  assertEquals(result.status, 200)
  assertEquals(result.stateChanged, true)

  // Should have read from embeddeds
  assertEquals(calls.selects.length, 1)
  assertEquals(calls.selects[0].table, 'embeddeds')

  // Should have updated embeddeds.mdb_diagnostics
  assertEquals(calls.updates.length, 1)
  assertEquals(calls.updates[0].table, 'embeddeds')
  assertExists(calls.updates[0].data.mdb_diagnostics)
  assertEquals((calls.updates[0].data.mdb_diagnostics as Record<string, unknown>).state, 'ENABLED')

  // Should have inserted into mdb_log
  assertEquals(calls.inserts.length, 1)
  assertEquals(calls.inserts[0].table, 'mdb_log')
  assertEquals(calls.inserts[0].data.state, 'ENABLED')
  assertEquals(calls.inserts[0].data.prev_state, 'DISABLED')
  assertEquals(calls.inserts[0].data.addr, '0x10')
  assertEquals(calls.inserts[0].data.polls, 1500)
  assertEquals(calls.inserts[0].data.chk_err, 0)
  assertEquals(calls.inserts[0].data.last_cmd, 'READER_ENABLE')
})

Deno.test('mdb-log: same state does NOT insert history row', async () => {
  const payload = { state: 'ENABLED', addr: '0x10', polls: 2000, chkErr: 0, lastCmd: 'READER_ENABLE' }
  const { client, calls } = createMockAdminClient({
    selectResult: {
      mdb_diagnostics: { state: 'ENABLED' },
      company: COMPANY_ID,
    },
  })

  const result = await handleMdbLog(encodePayload(payload), DEVICE_ID, client)

  assertEquals(result.status, 200)
  assertEquals(result.stateChanged, false)

  // Should still update embeddeds (latest polls/chkErr)
  assertEquals(calls.updates.length, 1)

  // Should NOT insert into mdb_log
  assertEquals(calls.inserts.length, 0)
})

Deno.test('mdb-log: first message ever (no prior diagnostics) inserts with prev_state=null', async () => {
  const payload = { state: 'INACTIVE', addr: '0x60', polls: 0, chkErr: 0, lastCmd: 'none' }
  const { client, calls } = createMockAdminClient({
    selectResult: {
      mdb_diagnostics: null,
      company: COMPANY_ID,
    },
  })

  const result = await handleMdbLog(encodePayload(payload), DEVICE_ID, client)

  assertEquals(result.status, 200)
  assertEquals(result.stateChanged, true)

  // History row should have prev_state = null
  assertEquals(calls.inserts.length, 1)
  assertEquals(calls.inserts[0].data.prev_state, null)
  assertEquals(calls.inserts[0].data.state, 'INACTIVE')
})

Deno.test('mdb-log: malformed JSON returns 400', async () => {
  const badPayload = encodeBase64(new TextEncoder().encode('not valid json {'))
  const { client } = createMockAdminClient({ selectResult: null })

  const result = await handleMdbLog(badPayload, DEVICE_ID, client)

  assertEquals(result.status, 400)
  assertEquals(result.body.error, 'invalid JSON in mdb-log payload')
})

Deno.test('mdb-log: missing state field returns 400', async () => {
  const payload = { addr: '0x10', polls: 100 } // no "state" key
  const { client } = createMockAdminClient({ selectResult: null })

  const result = await handleMdbLog(encodePayload(payload), DEVICE_ID, client)

  assertEquals(result.status, 400)
  assertEquals(result.body.error, 'missing state field in mdb-log')
})

Deno.test('mdb-log: chkErr maps to chk_err and lastCmd maps to last_cmd', async () => {
  const payload = { state: 'VEND', addr: '0x10', polls: 5000, chkErr: 42, lastCmd: 'VEND_REQUEST' }
  const { client, calls } = createMockAdminClient({
    selectResult: {
      mdb_diagnostics: { state: 'IDLE' },
      company: COMPANY_ID,
    },
  })

  const result = await handleMdbLog(encodePayload(payload), DEVICE_ID, client)

  assertEquals(result.status, 200)
  assertEquals(result.stateChanged, true)

  // Verify field mapping
  assertEquals(calls.inserts[0].data.chk_err, 42)
  assertEquals(calls.inserts[0].data.last_cmd, 'VEND_REQUEST')
  assertEquals(calls.inserts[0].data.state, 'VEND')
  assertEquals(calls.inserts[0].data.prev_state, 'IDLE')
})

Deno.test('mdb-log: device not found returns 404', async () => {
  const payload = { state: 'ENABLED', addr: '0x10', polls: 0, chkErr: 0, lastCmd: 'RESET' }
  const { client } = createMockAdminClient({
    selectResult: null,
  })

  const result = await handleMdbLog(encodePayload(payload), DEVICE_ID, client)

  assertEquals(result.status, 404)
  assertEquals(result.body.error, 'device not found')
})

Deno.test('mdb-log: raw field preserves full original payload', async () => {
  const payload = { state: 'ENABLED', addr: '0x10', polls: 100, chkErr: 0, lastCmd: 'SETUP:CONFIG_DATA', extra: 'future_field' }
  const { client, calls } = createMockAdminClient({
    selectResult: {
      mdb_diagnostics: { state: 'DISABLED' },
      company: COMPANY_ID,
    },
  })

  await handleMdbLog(encodePayload(payload), DEVICE_ID, client)

  // raw should contain the full original payload including unknown fields
  const raw = calls.inserts[0].data.raw as Record<string, unknown>
  assertEquals(raw.extra, 'future_field')
  assertEquals(raw.state, 'ENABLED')
})
