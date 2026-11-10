-- VMflow bootstrap: objects the app needs beyond the public schema dump.
-- Executed by the one-shot `bootstrap` service on every `docker compose up`,
-- after storage-api has run its own migrations. Must stay idempotent.

\set ON_ERROR_STOP on

-- Realtime: dashboard alerts (vuejs/src/lib/useAlerts.js) listen to UPDATEs on these tables.
DO $$
DECLARE t text;
BEGIN
  FOREACH t IN ARRAY ARRAY['embedded', 'machine_coils'] LOOP
    IF NOT EXISTS (
      SELECT 1 FROM pg_publication_tables
      WHERE pubname = 'supabase_realtime' AND schemaname = 'public' AND tablename = t
    ) THEN
      EXECUTE format('ALTER PUBLICATION supabase_realtime ADD TABLE public.%I', t);
    END IF;
  END LOOP;
END $$;

-- Storage: product images (Dashboard_Products.vue) live in a public bucket.
INSERT INTO storage.buckets (id, name, public)
VALUES ('product-images', 'product-images', true)
ON CONFLICT (id) DO NOTHING;

DROP POLICY IF EXISTS "product-images: authenticated upload" ON storage.objects;
CREATE POLICY "product-images: authenticated upload"
  ON storage.objects FOR INSERT TO authenticated
  WITH CHECK (bucket_id = 'product-images');

DROP POLICY IF EXISTS "product-images: authenticated update" ON storage.objects;
CREATE POLICY "product-images: authenticated update"
  ON storage.objects FOR UPDATE TO authenticated
  USING (bucket_id = 'product-images');

DROP POLICY IF EXISTS "product-images: authenticated delete" ON storage.objects;
CREATE POLICY "product-images: authenticated delete"
  ON storage.objects FOR DELETE TO authenticated
  USING (bucket_id = 'product-images');
