-- Sales were never mapped to products: sync_machine_coils_on_model_change copied
-- only alias/capacity from model_coils, leaving machine_coils.item_number NULL,
-- so fill_sale_coil_data (matches on item_number) never found a coil.
--
-- Also, remote-credit sales (send-credit / Stripe / MercadoPago / x402) were
-- inserted with item_number = 0 (column default) before any product is chosen.
-- Once coils carry item_number those rows would wrongly match coil 0, so
-- item_number becomes nullable: NULL = "item not known yet". The device reports
-- the vended item later (MQTT vend_success) and the row is updated in place.

BEGIN;

-- 1. Copy item_number when coils are replicated from the model.
CREATE OR REPLACE FUNCTION public.sync_machine_coils_on_model_change() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    -- Só executa se o model_id realmente mudou
    IF NEW.model_id IS DISTINCT FROM OLD.model_id THEN

        -- Remove coils antigos da máquina
        DELETE FROM machine_coils
        WHERE machine_id = NEW.id;

        -- Replica coils do novo modelo
        INSERT INTO machine_coils (machine_id, alias, item_number, capacity)
        SELECT
            NEW.id,
            mc.alias,
            mc.item_number,
            mc.capacity
        FROM model_coils mc
        WHERE mc.model_id = NEW.model_id;

    END IF;

    RETURN NEW;
END;
$$;

-- 2. Backfill machine_coils.item_number from the machine's model (match by alias).
UPDATE public.machine_coils c
SET item_number = (
    SELECT mc.item_number
    FROM public.machines m
    JOIN public.model_coils mc ON mc.model_id = m.model_id
    WHERE m.id = c.machine_id
      AND mc.alias = c.alias
      AND mc.item_number IS NOT NULL
    ORDER BY mc.created_at
    LIMIT 1
)
WHERE c.item_number IS NULL;

-- 3. item_number unknown for remote-credit sales until the device reports the vend.
ALTER TABLE public.sales ALTER COLUMN item_number DROP NOT NULL;
ALTER TABLE public.sales ALTER COLUMN item_number DROP DEFAULT;

UPDATE public.sales
SET item_number = NULL
WHERE channel = 'mqtt' AND item_number = 0 AND product_id IS NULL;

-- 4. Map coil on insert, and on the later NULL -> item update (vend_success).
CREATE OR REPLACE FUNCTION public.fill_sale_coil_data() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN

    IF NEW.item_number IS NULL THEN
        RETURN NEW;
    END IF;

    IF TG_OP = 'UPDATE' AND OLD.item_number IS NOT NULL THEN
        RETURN NEW;
    END IF;

    SELECT product_id, alias
    INTO NEW.product_id, NEW.coil_alias
    FROM "public"."machine_coils"
    WHERE machine_id = NEW.machine_id AND item_number = NEW.item_number
    LIMIT 1;

    UPDATE public.machine_coils set current_stock = greatest(0, current_stock - 1)
    where machine_id = NEW.machine_id and item_number = NEW.item_number;

    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_fill_sale_coil_data ON public.sales;
CREATE TRIGGER trg_fill_sale_coil_data
    BEFORE INSERT OR UPDATE OF item_number ON public.sales
    FOR EACH ROW EXECUTE FUNCTION public.fill_sale_coil_data();

-- 5. Backfill product/alias on past sales (no stock change: stock was never
--    debited for them and operators may have refilled since).
UPDATE public.sales s
SET product_id = c.product_id,
    coil_alias = c.alias
FROM public.machine_coils c
WHERE c.machine_id = s.machine_id
  AND c.item_number = s.item_number
  AND s.item_number IS NOT NULL
  AND s.product_id IS NULL;

COMMIT;
