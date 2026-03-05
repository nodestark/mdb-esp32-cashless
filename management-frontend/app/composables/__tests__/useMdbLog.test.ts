import { describe, it, expect, vi, beforeEach } from 'vitest'
import { stateLabel, stateVariant } from '../useMdbLog'

// ── Pure helper tests ─────────────────────────────────────────────────────────

describe('stateLabel', () => {
    it('maps known MDB states to human-readable labels', () => {
        expect(stateLabel('INACTIVE')).toBe('Inactive')
        expect(stateLabel('DISABLED')).toBe('Disabled')
        expect(stateLabel('ENABLED')).toBe('Enabled')
        expect(stateLabel('IDLE')).toBe('Idle')
        expect(stateLabel('VEND')).toBe('Vending')
        expect(stateLabel('UNKNOWN')).toBe('Unknown')
    })

    it('returns the raw string for unrecognised states', () => {
        expect(stateLabel('CUSTOM_STATE')).toBe('CUSTOM_STATE')
        expect(stateLabel('')).toBe('')
    })
})

describe('stateVariant', () => {
    it('maps INACTIVE to destructive (error indicator)', () => {
        expect(stateVariant('INACTIVE')).toBe('destructive')
    })

    it('maps DISABLED to outline', () => {
        expect(stateVariant('DISABLED')).toBe('outline')
    })

    it('maps ENABLED to secondary', () => {
        expect(stateVariant('ENABLED')).toBe('secondary')
    })

    it('maps IDLE and VEND to default', () => {
        expect(stateVariant('IDLE')).toBe('default')
        expect(stateVariant('VEND')).toBe('default')
    })

    it('falls back to outline for unknown states', () => {
        expect(stateVariant('WHATEVER')).toBe('outline')
        expect(stateVariant('')).toBe('outline')
    })
})

// ── Composable tests (with mocked Supabase) ─────────────────────────────────

const mockChannel = {
    on: vi.fn().mockReturnThis(),
    subscribe: vi.fn().mockReturnThis(),
}
const mockSupabase = {
    from: vi.fn().mockReturnThis(),
    select: vi.fn().mockReturnThis(),
    eq: vi.fn().mockReturnThis(),
    lt: vi.fn().mockReturnThis(),
    order: vi.fn().mockReturnThis(),
    limit: vi.fn().mockResolvedValue({ data: [], error: null }),
    channel: vi.fn().mockReturnValue(mockChannel),
    removeChannel: vi.fn(),
}

vi.mock('#imports', () => {
    const { ref } = require('vue')
    return {
        ref,
        useSupabaseClient: () => mockSupabase,
    }
})

describe('useMdbLog composable', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockSupabase.limit.mockResolvedValue({ data: [], error: null })
        // Re-wire chaining since clearAllMocks resets mockReturnThis
        mockSupabase.from.mockReturnThis()
        mockSupabase.select.mockReturnThis()
        mockSupabase.eq.mockReturnThis()
        mockSupabase.lt.mockReturnThis()
        mockSupabase.order.mockReturnThis()
        mockChannel.on.mockReturnThis()
        mockChannel.subscribe.mockReturnThis()
        mockSupabase.channel.mockReturnValue(mockChannel)
    })

    it('fetchLogs queries mdb_log table with correct filters', async () => {
        const testData = [
            {
                id: '1',
                created_at: '2026-03-05T10:00:00Z',
                embedded_id: 'dev-1',
                state: 'ENABLED',
                prev_state: 'DISABLED',
                addr: '0x10',
                polls: 100,
                chk_err: 0,
                last_cmd: 'READER_ENABLE',
                raw: {},
            },
        ]
        mockSupabase.limit.mockResolvedValueOnce({ data: testData, error: null })

        const { useMdbLog } = await import('../useMdbLog')
        const { logs, fetchLogs, hasMore } = useMdbLog()

        await fetchLogs('dev-1')

        expect(mockSupabase.from).toHaveBeenCalledWith('mdb_log')
        expect(mockSupabase.select).toHaveBeenCalledWith('*')
        expect(mockSupabase.eq).toHaveBeenCalledWith('embedded_id', 'dev-1')
        expect(mockSupabase.order).toHaveBeenCalledWith('created_at', { ascending: false })
        expect(mockSupabase.limit).toHaveBeenCalledWith(50)
        expect(logs.value).toHaveLength(1)
        expect(logs.value[0].state).toBe('ENABLED')
        expect(hasMore.value).toBe(false) // 1 result < PAGE_SIZE=50
    })

    it('fetchMore uses cursor-based pagination with lt()', async () => {
        // Page 1: exactly 50 items — last item has oldest timestamp
        const OLDEST_TIMESTAMP = '2026-03-05T08:30:00Z'
        const page1 = Array.from({ length: 50 }, (_, i) => ({
            id: `id-${i}`,
            created_at: i === 49 ? OLDEST_TIMESTAMP : `2026-03-05T09:${String(59 - i).padStart(2, '0')}:00Z`,
            embedded_id: 'dev-1',
            state: 'ENABLED',
            prev_state: null,
            addr: '0x10',
            polls: i,
            chk_err: 0,
            last_cmd: 'POLL',
            raw: {},
        }))

        mockSupabase.limit.mockResolvedValueOnce({ data: page1, error: null })

        const { useMdbLog } = await import('../useMdbLog')
        const { fetchLogs, fetchMore, hasMore } = useMdbLog()

        await fetchLogs('dev-1')
        expect(hasMore.value).toBe(true) // exactly PAGE_SIZE

        // Page 2: fewer results
        mockSupabase.limit.mockResolvedValueOnce({
            data: [{ id: 'page2-1', state: 'IDLE', created_at: '2026-03-05T07:00:00Z' }],
            error: null,
        })

        await fetchMore('dev-1')

        // lt() should have been called with the oldest timestamp from page 1
        expect(mockSupabase.lt).toHaveBeenCalledWith('created_at', OLDEST_TIMESTAMP)
    })

    it('subscribe creates realtime channel with correct filter and returns cleanup', async () => {
        const { useMdbLog } = await import('../useMdbLog')
        const { subscribe } = useMdbLog()

        const cleanup = subscribe('dev-42')

        expect(mockSupabase.channel).toHaveBeenCalledWith('mdb-log-dev-42')
        expect(mockChannel.on).toHaveBeenCalledWith(
            'postgres_changes',
            expect.objectContaining({
                event: 'INSERT',
                schema: 'public',
                table: 'mdb_log',
                filter: 'embedded_id=eq.dev-42',
            }),
            expect.any(Function),
        )
        expect(typeof cleanup).toBe('function')

        cleanup()
        expect(mockSupabase.removeChannel).toHaveBeenCalled()
    })

    it('fetchMore does nothing when hasMore is false', async () => {
        mockSupabase.limit.mockResolvedValueOnce({
            data: [{ id: '1', state: 'IDLE', created_at: '2026-03-05T10:00:00Z' }],
            error: null,
        })

        const { useMdbLog } = await import('../useMdbLog')
        const { fetchLogs, fetchMore, hasMore } = useMdbLog()

        await fetchLogs('dev-1')
        expect(hasMore.value).toBe(false)

        const callsBefore = mockSupabase.from.mock.calls.length
        await fetchMore('dev-1')

        // No additional from() calls
        expect(mockSupabase.from.mock.calls.length).toBe(callsBefore)
    })
})
