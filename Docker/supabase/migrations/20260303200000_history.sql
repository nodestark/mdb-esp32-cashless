-- Activity / audit log
-- Records platform-wide events: sales, credit sends, stock changes, etc.
-- Written by service_role (Edge Functions) or authenticated users.
-- Readable by any member of the same company.

CREATE TABLE IF NOT EXISTS public.activity_log (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    created_at  timestamptz NOT NULL DEFAULT now(),
    company_id  uuid        NOT NULL REFERENCES public.companies(id) ON DELETE CASCADE,
    user_id     uuid        DEFAULT auth.uid() REFERENCES auth.users(id) ON DELETE SET NULL,
    entity_type text        NOT NULL,  -- 'sale' | 'credit' | 'stock' | 'firmware' | 'device' | ...
    entity_id   text,                  -- optional: device/machine/tray ID
    action      text        NOT NULL,  -- short label e.g. 'sale_recorded', 'credit_sent', 'stock_updated'
    metadata    jsonb                  -- arbitrary extra details
);

ALTER TABLE public.activity_log ENABLE ROW LEVEL SECURITY;

-- Grant reads to authenticated users; all writes go through service_role (Edge Functions)
-- or authenticated users within their own company.
GRANT SELECT, INSERT ON public.activity_log TO authenticated;
GRANT ALL ON public.activity_log TO service_role;

-- Members of the same company can read the log
CREATE POLICY activity_log_select ON public.activity_log
    FOR SELECT TO authenticated
    USING (company_id = public.my_company_id());

-- Authenticated users may insert rows for their own company (e.g. stock changes from the UI)
CREATE POLICY activity_log_insert ON public.activity_log
    FOR INSERT TO authenticated
    WITH CHECK (company_id = public.my_company_id());

-- Enable realtime so the history page updates live
ALTER PUBLICATION supabase_realtime ADD TABLE public.activity_log;
