-- Add minimum stock threshold per tray (for refill alerts)
ALTER TABLE public.machine_trays
  ADD COLUMN IF NOT EXISTS min_stock integer NOT NULL DEFAULT 0;

COMMENT ON COLUMN public.machine_trays.min_stock IS
  'Minimum stock level before a refill is needed. 0 = no threshold.';
