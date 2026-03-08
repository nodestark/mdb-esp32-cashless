-- Add source tracking columns to firmware_versions
-- Allows distinguishing manually uploaded firmware from GitHub release imports
ALTER TABLE public.firmware_versions
  ADD COLUMN IF NOT EXISTS source_type text DEFAULT 'upload',
  ADD COLUMN IF NOT EXISTS source_tag  text;

-- source_type: 'upload' (manual) or 'github' (imported from GitHub release)
-- source_tag:  GitHub release tag (e.g. 'slave-v1.2.3'), NULL for manual uploads
COMMENT ON COLUMN public.firmware_versions.source_type IS 'Origin: upload or github';
COMMENT ON COLUMN public.firmware_versions.source_tag  IS 'GitHub release tag if source_type=github';
