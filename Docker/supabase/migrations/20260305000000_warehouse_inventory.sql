-- ============================================================================
-- Warehouse Inventory Management
-- Tables: warehouses, product_barcodes, warehouse_stock_batches,
--         warehouse_transactions, product_min_stock
-- Function: deduct_warehouse_stock_fifo (FIFO deduction for refills)
-- ============================================================================

-- ── warehouses ──────────────────────────────────────────────────────────────
CREATE TABLE public.warehouses (
  id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  created_at    timestamptz NOT NULL DEFAULT now(),
  company_id    uuid        NOT NULL REFERENCES public.companies(id) ON DELETE CASCADE,
  name          text        NOT NULL,
  address       text,
  notes         text,
  CONSTRAINT warehouses_company_name_unique UNIQUE (company_id, name)
);

ALTER TABLE public.warehouses ENABLE ROW LEVEL SECURITY;

GRANT SELECT, INSERT, UPDATE, DELETE ON public.warehouses TO authenticated;
GRANT ALL ON public.warehouses TO service_role;

CREATE POLICY warehouses_select ON public.warehouses
  FOR SELECT TO authenticated
  USING (company_id = public.my_company_id());

CREATE POLICY warehouses_insert ON public.warehouses
  FOR INSERT TO authenticated
  WITH CHECK (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY warehouses_update ON public.warehouses
  FOR UPDATE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY warehouses_delete ON public.warehouses
  FOR DELETE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

-- ── product_barcodes ────────────────────────────────────────────────────────
CREATE TABLE public.product_barcodes (
  id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  created_at    timestamptz NOT NULL DEFAULT now(),
  product_id    uuid        NOT NULL REFERENCES public.products(id) ON DELETE CASCADE,
  barcode       text        NOT NULL,
  format        text        NOT NULL DEFAULT 'EAN-13',
  company_id    uuid        NOT NULL REFERENCES public.companies(id) ON DELETE CASCADE,
  CONSTRAINT product_barcodes_company_barcode_unique UNIQUE (company_id, barcode)
);

CREATE INDEX idx_product_barcodes_barcode ON public.product_barcodes (barcode);
CREATE INDEX idx_product_barcodes_product ON public.product_barcodes (product_id);

ALTER TABLE public.product_barcodes ENABLE ROW LEVEL SECURITY;

GRANT SELECT, INSERT, UPDATE, DELETE ON public.product_barcodes TO authenticated;
GRANT ALL ON public.product_barcodes TO service_role;

CREATE POLICY product_barcodes_select ON public.product_barcodes
  FOR SELECT TO authenticated
  USING (company_id = public.my_company_id());

CREATE POLICY product_barcodes_insert ON public.product_barcodes
  FOR INSERT TO authenticated
  WITH CHECK (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY product_barcodes_update ON public.product_barcodes
  FOR UPDATE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY product_barcodes_delete ON public.product_barcodes
  FOR DELETE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

-- ── warehouse_stock_batches ─────────────────────────────────────────────────
CREATE TABLE public.warehouse_stock_batches (
  id              uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  created_at      timestamptz NOT NULL DEFAULT now(),
  warehouse_id    uuid        NOT NULL REFERENCES public.warehouses(id) ON DELETE CASCADE,
  product_id      uuid        NOT NULL REFERENCES public.products(id) ON DELETE CASCADE,
  batch_number    text,
  expiration_date date,
  quantity        integer     NOT NULL DEFAULT 0,
  company_id      uuid        NOT NULL REFERENCES public.companies(id) ON DELETE CASCADE,
  CONSTRAINT wsb_quantity_non_negative CHECK (quantity >= 0)
);

CREATE INDEX idx_wsb_warehouse_product ON public.warehouse_stock_batches (warehouse_id, product_id);
CREATE INDEX idx_wsb_expiration ON public.warehouse_stock_batches (expiration_date);
CREATE INDEX idx_wsb_company ON public.warehouse_stock_batches (company_id);

ALTER TABLE public.warehouse_stock_batches ENABLE ROW LEVEL SECURITY;

GRANT SELECT, INSERT, UPDATE, DELETE ON public.warehouse_stock_batches TO authenticated;
GRANT ALL ON public.warehouse_stock_batches TO service_role;

CREATE POLICY wsb_select ON public.warehouse_stock_batches
  FOR SELECT TO authenticated
  USING (company_id = public.my_company_id());

CREATE POLICY wsb_insert ON public.warehouse_stock_batches
  FOR INSERT TO authenticated
  WITH CHECK (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY wsb_update ON public.warehouse_stock_batches
  FOR UPDATE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY wsb_delete ON public.warehouse_stock_batches
  FOR DELETE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

-- ── warehouse_transactions ──────────────────────────────────────────────────
CREATE TABLE public.warehouse_transactions (
  id              uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  created_at      timestamptz NOT NULL DEFAULT now(),
  company_id      uuid        NOT NULL REFERENCES public.companies(id) ON DELETE CASCADE,
  warehouse_id    uuid        NOT NULL REFERENCES public.warehouses(id) ON DELETE CASCADE,
  product_id      uuid        NOT NULL REFERENCES public.products(id) ON DELETE CASCADE,
  batch_id        uuid        REFERENCES public.warehouse_stock_batches(id) ON DELETE SET NULL,
  user_id         uuid        REFERENCES auth.users(id) ON DELETE SET NULL,
  transaction_type text       NOT NULL,
  quantity_change  integer    NOT NULL,
  quantity_before  integer,
  quantity_after   integer,
  batch_number     text,
  expiration_date  date,
  reference_id     text,
  notes            text,
  metadata         jsonb
);

CREATE INDEX idx_wt_warehouse ON public.warehouse_transactions (warehouse_id);
CREATE INDEX idx_wt_product ON public.warehouse_transactions (product_id);
CREATE INDEX idx_wt_company_created ON public.warehouse_transactions (company_id, created_at DESC);
CREATE INDEX idx_wt_type ON public.warehouse_transactions (transaction_type);

ALTER TABLE public.warehouse_transactions ENABLE ROW LEVEL SECURITY;

GRANT SELECT, INSERT ON public.warehouse_transactions TO authenticated;
GRANT ALL ON public.warehouse_transactions TO service_role;

CREATE POLICY wt_select ON public.warehouse_transactions
  FOR SELECT TO authenticated
  USING (company_id = public.my_company_id());

CREATE POLICY wt_insert ON public.warehouse_transactions
  FOR INSERT TO authenticated
  WITH CHECK (company_id = (SELECT public.my_company_id()));

-- ── product_min_stock ───────────────────────────────────────────────────────
CREATE TABLE public.product_min_stock (
  id              uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  product_id      uuid        NOT NULL REFERENCES public.products(id) ON DELETE CASCADE,
  warehouse_id    uuid        NOT NULL REFERENCES public.warehouses(id) ON DELETE CASCADE,
  min_quantity    integer     NOT NULL DEFAULT 0,
  company_id      uuid        NOT NULL REFERENCES public.companies(id) ON DELETE CASCADE,
  CONSTRAINT pms_product_warehouse_unique UNIQUE (product_id, warehouse_id)
);

ALTER TABLE public.product_min_stock ENABLE ROW LEVEL SECURITY;

GRANT SELECT, INSERT, UPDATE, DELETE ON public.product_min_stock TO authenticated;
GRANT ALL ON public.product_min_stock TO service_role;

CREATE POLICY pms_select ON public.product_min_stock
  FOR SELECT TO authenticated
  USING (company_id = public.my_company_id());

CREATE POLICY pms_insert ON public.product_min_stock
  FOR INSERT TO authenticated
  WITH CHECK (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY pms_update ON public.product_min_stock
  FOR UPDATE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

CREATE POLICY pms_delete ON public.product_min_stock
  FOR DELETE TO authenticated
  USING (company_id = (SELECT public.my_company_id()) AND (SELECT public.i_am_admin()));

-- ── FIFO deduction function ─────────────────────────────────────────────────
CREATE OR REPLACE FUNCTION public.deduct_warehouse_stock_fifo(
  p_warehouse_id uuid,
  p_product_id   uuid,
  p_quantity     integer,
  p_user_id      uuid DEFAULT NULL,
  p_reference_id text DEFAULT NULL,
  p_notes        text DEFAULT NULL,
  p_metadata     jsonb DEFAULT NULL
)
RETURNS jsonb
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = ''
AS $$
DECLARE
  v_remaining   integer := p_quantity;
  v_batch       record;
  v_deducted    integer;
  v_result      jsonb := '[]'::jsonb;
  v_company_id  uuid;
BEGIN
  SELECT w.company_id INTO v_company_id
  FROM public.warehouses w
  WHERE w.id = p_warehouse_id;

  IF v_company_id IS NULL THEN
    RAISE EXCEPTION 'Warehouse not found';
  END IF;

  FOR v_batch IN
    SELECT id, batch_number, expiration_date, quantity
    FROM public.warehouse_stock_batches
    WHERE warehouse_id = p_warehouse_id
      AND product_id = p_product_id
      AND quantity > 0
    ORDER BY expiration_date ASC NULLS LAST, created_at ASC
    FOR UPDATE
  LOOP
    EXIT WHEN v_remaining <= 0;

    v_deducted := LEAST(v_batch.quantity, v_remaining);

    UPDATE public.warehouse_stock_batches
    SET quantity = quantity - v_deducted
    WHERE id = v_batch.id;

    INSERT INTO public.warehouse_transactions (
      company_id, warehouse_id, product_id, batch_id, user_id,
      transaction_type, quantity_change, quantity_before, quantity_after,
      batch_number, expiration_date, reference_id, notes, metadata
    ) VALUES (
      v_company_id, p_warehouse_id, p_product_id, v_batch.id, p_user_id,
      'outgoing_refill', -v_deducted, v_batch.quantity, v_batch.quantity - v_deducted,
      v_batch.batch_number, v_batch.expiration_date, p_reference_id, p_notes, p_metadata
    );

    v_result := v_result || jsonb_build_object(
      'batch_id', v_batch.id,
      'batch_number', v_batch.batch_number,
      'expiration_date', v_batch.expiration_date,
      'deducted', v_deducted
    );

    v_remaining := v_remaining - v_deducted;
  END LOOP;

  IF v_remaining > 0 THEN
    RAISE EXCEPTION 'Insufficient warehouse stock. Short by % units.', v_remaining;
  END IF;

  RETURN v_result;
END;
$$;

GRANT EXECUTE ON FUNCTION public.deduct_warehouse_stock_fifo TO authenticated;

-- ── Realtime ────────────────────────────────────────────────────────────────
ALTER PUBLICATION supabase_realtime ADD TABLE public.warehouse_stock_batches;
ALTER PUBLICATION supabase_realtime ADD TABLE public.warehouse_transactions;
ALTER PUBLICATION supabase_realtime ADD TABLE public.warehouses;
