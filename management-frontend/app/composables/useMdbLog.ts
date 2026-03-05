import { ref, useSupabaseClient } from '#imports'

export interface MdbLogEntry {
    id: string
    created_at: string
    embedded_id: string
    state: string
    prev_state: string | null
    addr: string | null
    polls: number | null
    chk_err: number | null
    last_cmd: string | null
    raw: Record<string, unknown> | null
}

export interface MdbDiagnostics {
    state: string
    addr: string
    polls: number
    chkErr: number
    lastCmd: string
    updated_at: string
}

const PAGE_SIZE = 50

export function useMdbLog() {
    const supabase = useSupabaseClient()

    const logs = ref<MdbLogEntry[]>([])
    const loading = ref(false)
    const hasMore = ref(true)

    async function fetchLogs(embeddedId: string) {
        loading.value = true
        hasMore.value = true
        try {
            const { data, error } = await (supabase as any)
                .from('mdb_log')
                .select('*')
                .eq('embedded_id', embeddedId)
                .order('created_at', { ascending: false })
                .limit(PAGE_SIZE)

            if (error) throw error
            logs.value = (data ?? []) as MdbLogEntry[]
            hasMore.value = logs.value.length === PAGE_SIZE
        } finally {
            loading.value = false
        }
    }

    async function fetchMore(embeddedId: string) {
        if (!hasMore.value || loading.value) return
        loading.value = true
        try {
            const oldest = logs.value[logs.value.length - 1]?.created_at
            if (!oldest) return

            const { data, error } = await (supabase as any)
                .from('mdb_log')
                .select('*')
                .eq('embedded_id', embeddedId)
                .lt('created_at', oldest)
                .order('created_at', { ascending: false })
                .limit(PAGE_SIZE)

            if (error) throw error
            const next = (data ?? []) as MdbLogEntry[]
            logs.value.push(...next)
            hasMore.value = next.length === PAGE_SIZE
        } finally {
            loading.value = false
        }
    }

    function subscribe(embeddedId: string) {
        const channel = (supabase as any)
            .channel(`mdb-log-${embeddedId}`)
            .on(
                'postgres_changes',
                {
                    event: 'INSERT',
                    schema: 'public',
                    table: 'mdb_log',
                    filter: `embedded_id=eq.${embeddedId}`,
                },
                (payload: { new: MdbLogEntry }) => {
                    logs.value.unshift(payload.new)
                },
            )
            .subscribe()

        return () => (supabase as any).removeChannel(channel)
    }

    return {
        logs,
        loading,
        hasMore,
        fetchLogs,
        fetchMore,
        subscribe,
        stateLabel,
        stateVariant,
    }
}

// ── Exported helpers (pure functions, easy to test) ──────────────────────────

export function stateLabel(state: string): string {
    const labels: Record<string, string> = {
        INACTIVE: 'Inactive',
        DISABLED: 'Disabled',
        ENABLED: 'Enabled',
        IDLE: 'Idle',
        VEND: 'Vending',
        UNKNOWN: 'Unknown',
    }
    return labels[state] ?? state
}

export function stateVariant(state: string): 'default' | 'secondary' | 'destructive' | 'outline' {
    const map: Record<string, 'default' | 'secondary' | 'destructive' | 'outline'> = {
        INACTIVE: 'destructive',
        DISABLED: 'outline',
        ENABLED: 'secondary',
        IDLE: 'default',
        VEND: 'default',
    }
    return map[state] ?? 'outline'
}
