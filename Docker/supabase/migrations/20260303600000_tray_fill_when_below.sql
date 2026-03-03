-- Add soft refill threshold per tray (include in packing list when operator is already visiting)
ALTER TABLE public.machine_trays
  ADD COLUMN IF NOT EXISTS fill_when_below integer NOT NULL DEFAULT 0;

COMMENT ON COLUMN public.machine_trays.fill_when_below IS
  'Soft stock threshold. When another tray triggers a refill trip (below min_stock), trays below fill_when_below are also included in the packing list. 0 = disabled.';
