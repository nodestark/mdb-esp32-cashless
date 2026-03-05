-- MDB diagnostics history
-- Stores MDB state-change events from ESP32 devices.
-- The latest diagnostics snapshot is kept on the embeddeds row for quick access.
-- History rows are inserted only when the MDB state machine changes state.

-- 1. Add latest-snapshot column to embeddeds
ALTER TABLE public.embeddeds
    ADD COLUMN IF NOT EXISTS mdb_diagnostics jsonb;

COMMENT ON COLUMN public.embeddeds.mdb_diagnostics
    IS 'Latest MDB diagnostics snapshot: {state, addr, polls, chkErr, lastCmd, updated_at}';

-- 2. State-change history table
CREATE TABLE IF NOT EXISTS public.mdb_log (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    created_at  timestamptz NOT NULL DEFAULT now(),
    embedded_id uuid        NOT NULL REFERENCES public.embeddeds(id) ON DELETE CASCADE,
    state       text        NOT NULL,    -- MDB state at the time of change
    prev_state  text,                    -- previous MDB state (NULL for first entry)
    addr        text,                    -- MDB address string e.g. "0x10"
    polls       integer,                 -- poll count since boot
    chk_err     integer,                 -- checksum error count since boot
    last_cmd    text,                    -- last MDB command received
    raw         jsonb                    -- full original JSON payload for extensibility
);

ALTER TABLE public.mdb_log ENABLE ROW LEVEL SECURITY;

-- Only service_role writes (via mqtt-webhook); authenticated users read
GRANT SELECT ON public.mdb_log TO authenticated;
GRANT ALL ON public.mdb_log TO service_role;

-- Members of the same company can read logs for their devices
CREATE POLICY mdb_log_select ON public.mdb_log
    FOR SELECT TO authenticated
    USING (
        EXISTS (
            SELECT 1 FROM public.embeddeds e
            WHERE e.id = mdb_log.embedded_id
              AND e.company = public.my_company_id()
        )
    );

-- Index for efficient per-device queries ordered by time
CREATE INDEX idx_mdb_log_embedded_created
    ON public.mdb_log (embedded_id, created_at DESC);

-- Enable realtime for live updates on the frontend
ALTER PUBLICATION supabase_realtime ADD TABLE public.mdb_log;
