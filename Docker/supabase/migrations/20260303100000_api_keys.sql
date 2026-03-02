-- API keys for external developer access (e.g. send-credit)
CREATE TABLE api_keys (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  company_id UUID NOT NULL REFERENCES companies(id) ON DELETE CASCADE,
  created_by UUID NOT NULL REFERENCES auth.users(id),
  name TEXT NOT NULL,
  key_prefix TEXT NOT NULL,
  key_hash TEXT NOT NULL UNIQUE,
  created_at TIMESTAMPTZ DEFAULT now(),
  last_used_at TIMESTAMPTZ,
  revoked_at TIMESTAMPTZ
);

ALTER TABLE api_keys ENABLE ROW LEVEL SECURITY;

CREATE POLICY "select own company"
  ON api_keys FOR SELECT
  USING (company_id = public.my_company_id());

CREATE POLICY "admin update"
  ON api_keys FOR UPDATE
  USING (company_id = public.my_company_id() AND public.i_am_admin());

CREATE POLICY "admin delete"
  ON api_keys FOR DELETE
  USING (company_id = public.my_company_id() AND public.i_am_admin());
