-- Add MDB bus address setting to embeddeds
-- 1 = Cashless Device #1 (0x10), 2 = Cashless Device #2 (0x60)
ALTER TABLE public.embeddeds
  ADD COLUMN IF NOT EXISTS mdb_address smallint NOT NULL DEFAULT 1
  CONSTRAINT mdb_address_valid CHECK (mdb_address IN (1, 2));

COMMENT ON COLUMN public.embeddeds.mdb_address
  IS 'MDB cashless device address: 1 = Device #1 (0x10), 2 = Device #2 (0x60)';
