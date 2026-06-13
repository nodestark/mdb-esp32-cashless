-- Consolidated baseline of the public schema (live database).
-- Squashes prior migrations into one snapshot. n8n triggers excluded.

--
-- PostgreSQL database dump
--

-- Dumped from database version 15.8
-- Dumped by pg_dump version 15.8

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: public; Type: SCHEMA; Schema: -; Owner: -
--


--
-- Name: SCHEMA public; Type: COMMENT; Schema: -; Owner: -
--

COMMENT ON SCHEMA public IS 'standard public schema';


--
-- Name: embedded_status; Type: TYPE; Schema: public; Owner: -
--

CREATE TYPE public.embedded_status AS ENUM (
    'online',
    'offline'
);


--
-- Name: machine_category; Type: TYPE; Schema: public; Owner: -
--

CREATE TYPE public.machine_category AS ENUM (
    'snack',
    'drink',
    'frozen',
    'candy',
    'personal_care',
    'other'
);


--
-- Name: metric_name; Type: TYPE; Schema: public; Owner: -
--

CREATE TYPE public.metric_name AS ENUM (
    'paxcounter'
);


--
-- Name: metric_unit; Type: TYPE; Schema: public; Owner: -
--

CREATE TYPE public.metric_unit AS ENUM (
    '-'
);


--
-- Name: sale_channel; Type: TYPE; Schema: public; Owner: -
--

CREATE TYPE public.sale_channel AS ENUM (
    'ble',
    'mqtt',
    'cash'
);


--
-- Name: bind_embedded_machine(uuid, uuid); Type: FUNCTION; Schema: public; Owner: -
--

CREATE OR REPLACE FUNCTION public.bind_embedded_machine(embedded_id_ uuid, machine_id_ uuid) RETURNS void
    LANGUAGE plpgsql
    AS $$
begin
	update embedded set machine_id = null where machine_id = machine_id_;
	update embedded set machine_id = machine_id_ where id = embedded_id_;
end;
$$;


--
-- Name: debit_product_stock_on_coil_refill(); Type: FUNCTION; Schema: public; Owner: -
--

CREATE OR REPLACE FUNCTION public.debit_product_stock_on_coil_refill() RETURNS trigger
    LANGUAGE plpgsql
    AS $$                                                            
  DECLARE
      v_diff integer;                                                                               
  BEGIN                                                     
      v_diff := NEW.current_stock - OLD.current_stock;
                                                                                                    
      IF v_diff > 0 AND NEW.product_id IS NOT NULL THEN
          UPDATE public.products                                                                    
          SET current_stock = GREATEST(0, current_stock - v_diff)
          WHERE id = NEW.product_id;
      END IF;

      RETURN NEW;
  END;
  $$;


--
-- Name: fill_sale_coil_data(); Type: FUNCTION; Schema: public; Owner: -
--

CREATE OR REPLACE FUNCTION public.fill_sale_coil_data() RETURNS trigger
    LANGUAGE plpgsql
    AS $$BEGIN

    SELECT product_id, alias
    INTO NEW.product_id, NEW.coil_alias
    FROM "public"."machine_coils"
    WHERE machine_id = NEW.machine_id AND item_number = NEW.item_number
    LIMIT 1;

    UPDATE public.machine_coils set current_stock = greatest(0, current_stock - 1)
    where machine_id = NEW.machine_id and item_number = NEW.item_number;

    RETURN NEW;
END;$$;


--
-- Name: get_machine_name(uuid); Type: FUNCTION; Schema: public; Owner: -
--

CREATE OR REPLACE FUNCTION public.get_machine_name(machine_id uuid) RETURNS text
    LANGUAGE sql SECURITY DEFINER
    SET search_path TO 'public'
    AS $$
  SELECT name FROM public.machines WHERE id = machine_id;
$$;


--
-- Name: sync_machine_coils_on_model_change(); Type: FUNCTION; Schema: public; Owner: -
--

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
        INSERT INTO machine_coils (machine_id, alias, capacity)
        SELECT
            NEW.id,
            mc.alias,
            mc.capacity
        FROM model_coils mc
        WHERE mc.model_id = NEW.model_id;

    END IF;

    RETURN NEW;
END;
$$;


SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: credentials; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.credentials (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    owner_id uuid DEFAULT auth.uid() NOT NULL,
    key text NOT NULL,
    value text NOT NULL
);


--
-- Name: embedded; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.embedded (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    owner_id uuid DEFAULT auth.uid(),
    machine_id uuid,
    subdomain bigint NOT NULL,
    mac_address text,
    status public.embedded_status DEFAULT 'offline'::public.embedded_status NOT NULL,
    status_at timestamp with time zone DEFAULT (now() AT TIME ZONE 'utc'::text) NOT NULL,
    passkey text DEFAULT SUBSTRING(md5((random())::text) FROM 1 FOR 18) NOT NULL
);


--
-- Name: embeddeds_subdomain_seq; Type: SEQUENCE; Schema: public; Owner: -
--

ALTER TABLE public.embedded ALTER COLUMN subdomain ADD GENERATED BY DEFAULT AS IDENTITY (
    SEQUENCE NAME public.embeddeds_subdomain_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);


--
-- Name: machine_coils; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.machine_coils (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    product_id uuid,
    item_number smallint,
    alias text,
    capacity integer,
    machine_id uuid,
    item_price real DEFAULT '0'::real NOT NULL,
    current_stock integer DEFAULT 0 NOT NULL
);


--
-- Name: machine_models; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.machine_models (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    name text,
    manufacturer text,
    owner_id uuid DEFAULT auth.uid(),
    enabled boolean DEFAULT true NOT NULL
);


--
-- Name: machines; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.machines (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    owner_id uuid DEFAULT auth.uid(),
    serial_number text,
    name text,
    refilled_at timestamp with time zone DEFAULT (now() AT TIME ZONE 'utc'::text) NOT NULL,
    model_id uuid,
    lat double precision,
    lng double precision,
    monthly_rent real DEFAULT '0'::real,
    category public.machine_category DEFAULT 'other'::public.machine_category,
    location_name text,
    address text
);


--
-- Name: metrics; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.metrics (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    machine_id uuid,
    name public.metric_name,
    unit public.metric_unit DEFAULT '-'::public.metric_unit,
    value real,
    payload jsonb,
    embedded_id uuid
)
PARTITION BY RANGE (created_at);


--
-- Name: metrics_2026_2036; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.metrics_2026_2036 (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    machine_id uuid,
    name public.metric_name,
    unit public.metric_unit DEFAULT '-'::public.metric_unit,
    value real,
    payload jsonb,
    embedded_id uuid
);


--
-- Name: model_coils; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.model_coils (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    alias text,
    item_number smallint,
    capacity smallint,
    model_id uuid
);


--
-- Name: payments; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.payments (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL
);


--
-- Name: products; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.products (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    name text,
    barcode text,
    owner_id uuid DEFAULT auth.uid() NOT NULL,
    image_url text,
    price real,
    enabled boolean DEFAULT true NOT NULL,
    current_stock integer DEFAULT 0 NOT NULL
);


--
-- Name: sales; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.sales (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    embedded_id uuid NOT NULL,
    machine_id uuid,
    product_id uuid,
    item_price double precision DEFAULT '0'::double precision NOT NULL,
    item_number bigint DEFAULT '0'::bigint NOT NULL,
    channel public.sale_channel NOT NULL,
    owner_id uuid DEFAULT auth.uid(),
    lat double precision,
    lng double precision,
    coil_alias text
);


--
-- Name: sales_metrics_v1; Type: VIEW; Schema: public; Owner: -
--

CREATE OR REPLACE VIEW public.sales_metrics_v1 WITH (security_invoker='on') AS
 SELECT s.machine_id,
    date_trunc('hour'::text, s.created_at) AS created_at,
    count(*) AS total
   FROM public.sales s
  GROUP BY s.machine_id, (date_trunc('hour'::text, s.created_at));


--
-- Name: metrics_2026_2036; Type: TABLE ATTACH; Schema: public; Owner: -
--

ALTER TABLE ONLY public.metrics ATTACH PARTITION public.metrics_2026_2036 FOR VALUES FROM ('2026-01-01 00:00:00+00') TO ('2036-01-01 00:00:00+00');


--
-- Name: credentials credentials_owner_id_key_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.credentials
    ADD CONSTRAINT credentials_owner_id_key_key UNIQUE (owner_id, key);


--
-- Name: credentials credentials_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.credentials
    ADD CONSTRAINT credentials_pkey PRIMARY KEY (id);


--
-- Name: embedded embedded_machine_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.embedded
    ADD CONSTRAINT embedded_machine_id_key UNIQUE (machine_id);


--
-- Name: embedded embeddeds_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.embedded
    ADD CONSTRAINT embeddeds_pkey PRIMARY KEY (id);


--
-- Name: machine_coils machine_coils_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.machine_coils
    ADD CONSTRAINT machine_coils_pkey PRIMARY KEY (id);


--
-- Name: machine_models machine_models_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.machine_models
    ADD CONSTRAINT machine_models_pkey PRIMARY KEY (id);


--
-- Name: machines machine_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.machines
    ADD CONSTRAINT machine_pkey PRIMARY KEY (id);


--
-- Name: model_coils model_coils_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.model_coils
    ADD CONSTRAINT model_coils_pkey PRIMARY KEY (id);


--
-- Name: payments payments_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.payments
    ADD CONSTRAINT payments_pkey PRIMARY KEY (id);


--
-- Name: products product_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.products
    ADD CONSTRAINT product_pkey PRIMARY KEY (id);


--
-- Name: sales sale_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.sales
    ADD CONSTRAINT sale_pkey PRIMARY KEY (id);


--
-- Name: machine_coils trg_debit_product_stock_on_coil_refill; Type: TRIGGER; Schema: public; Owner: -
--

CREATE TRIGGER trg_debit_product_stock_on_coil_refill AFTER UPDATE OF current_stock ON public.machine_coils FOR EACH ROW WHEN ((new.current_stock > old.current_stock)) EXECUTE FUNCTION public.debit_product_stock_on_coil_refill();


--
-- Name: sales trg_fill_sale_coil_data; Type: TRIGGER; Schema: public; Owner: -
--

CREATE TRIGGER trg_fill_sale_coil_data BEFORE INSERT ON public.sales FOR EACH ROW EXECUTE FUNCTION public.fill_sale_coil_data();


--
-- Name: machines trg_sync_machine_coils; Type: TRIGGER; Schema: public; Owner: -
--

CREATE TRIGGER trg_sync_machine_coils AFTER UPDATE OF model_id ON public.machines FOR EACH ROW EXECUTE FUNCTION public.sync_machine_coils_on_model_change();


--
-- Name: embedded embedded_machine_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.embedded
    ADD CONSTRAINT embedded_machine_id_fkey FOREIGN KEY (machine_id) REFERENCES public.machines(id);


--
-- Name: machine_coils fk_machine_coils_machine; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.machine_coils
    ADD CONSTRAINT fk_machine_coils_machine FOREIGN KEY (machine_id) REFERENCES public.machines(id);


--
-- Name: machine_coils fk_machine_coils_product; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.machine_coils
    ADD CONSTRAINT fk_machine_coils_product FOREIGN KEY (product_id) REFERENCES public.products(id);


--
-- Name: machines machines_model_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.machines
    ADD CONSTRAINT machines_model_id_fkey FOREIGN KEY (model_id) REFERENCES public.machine_models(id);


--
-- Name: model_coils model_coils_model_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.model_coils
    ADD CONSTRAINT model_coils_model_id_fkey FOREIGN KEY (model_id) REFERENCES public.machine_models(id);


--
-- Name: sales sale_machine_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.sales
    ADD CONSTRAINT sale_machine_id_fkey FOREIGN KEY (machine_id) REFERENCES public.machines(id);


--
-- Name: sales sale_product_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.sales
    ADD CONSTRAINT sale_product_id_fkey FOREIGN KEY (product_id) REFERENCES public.products(id);


--
-- Name: sales sales_embedded_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.sales
    ADD CONSTRAINT sales_embedded_id_fkey FOREIGN KEY (embedded_id) REFERENCES public.embedded(id);


--
-- Name: credentials; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.credentials ENABLE ROW LEVEL SECURITY;

--
-- Name: credentials credentials_delete; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY credentials_delete ON public.credentials FOR DELETE TO authenticated USING ((owner_id = auth.uid()));


--
-- Name: credentials credentials_insert; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY credentials_insert ON public.credentials FOR INSERT TO authenticated WITH CHECK ((owner_id = auth.uid()));


--
-- Name: credentials credentials_select; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY credentials_select ON public.credentials FOR SELECT TO authenticated USING ((owner_id = auth.uid()));


--
-- Name: credentials credentials_update; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY credentials_update ON public.credentials FOR UPDATE TO authenticated USING ((owner_id = auth.uid())) WITH CHECK ((owner_id = auth.uid()));


--
-- Name: embedded; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.embedded ENABLE ROW LEVEL SECURITY;

--
-- Name: embedded insert_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY insert_policy ON public.embedded FOR INSERT TO authenticated WITH CHECK (true);


--
-- Name: machine_models insert_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY insert_policy ON public.machine_models FOR INSERT TO authenticated WITH CHECK (true);


--
-- Name: machines insert_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY insert_policy ON public.machines FOR INSERT TO authenticated WITH CHECK (true);


--
-- Name: model_coils insert_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY insert_policy ON public.model_coils FOR INSERT TO authenticated WITH CHECK (true);


--
-- Name: products insert_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY insert_policy ON public.products FOR INSERT TO authenticated WITH CHECK (true);


--
-- Name: sales insert_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY insert_policy ON public.sales FOR INSERT TO authenticated WITH CHECK (true);


--
-- Name: machine_coils; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.machine_coils ENABLE ROW LEVEL SECURITY;

--
-- Name: machine_models; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.machine_models ENABLE ROW LEVEL SECURITY;

--
-- Name: machines; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.machines ENABLE ROW LEVEL SECURITY;

--
-- Name: metrics; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.metrics ENABLE ROW LEVEL SECURITY;

--
-- Name: model_coils; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.model_coils ENABLE ROW LEVEL SECURITY;

--
-- Name: payments; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.payments ENABLE ROW LEVEL SECURITY;

--
-- Name: machine_coils policy_insert; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY policy_insert ON public.machine_coils FOR INSERT TO authenticated WITH CHECK (true);


--
-- Name: metrics policy_select; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY policy_select ON public.metrics FOR SELECT TO authenticated USING ((EXISTS ( SELECT 1
   FROM public.embedded e
  WHERE ((e.id = metrics.embedded_id) AND (e.owner_id = auth.uid())))));


--
-- Name: products; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.products ENABLE ROW LEVEL SECURITY;

--
-- Name: sales; Type: ROW SECURITY; Schema: public; Owner: -
--

ALTER TABLE public.sales ENABLE ROW LEVEL SECURITY;

--
-- Name: embedded select_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY select_policy ON public.embedded FOR SELECT TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: machine_coils select_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY select_policy ON public.machine_coils FOR SELECT TO authenticated USING ((EXISTS ( SELECT 1
   FROM public.machines m
  WHERE ((m.id = machine_coils.machine_id) AND (m.owner_id = auth.uid())))));


--
-- Name: machine_models select_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY select_policy ON public.machine_models FOR SELECT TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: machines select_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY select_policy ON public.machines FOR SELECT TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: model_coils select_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY select_policy ON public.model_coils FOR SELECT TO authenticated USING ((EXISTS ( SELECT 1
   FROM public.machine_models m
  WHERE ((m.id = model_coils.model_id) AND (m.owner_id = auth.uid())))));


--
-- Name: products select_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY select_policy ON public.products FOR SELECT TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: sales select_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY select_policy ON public.sales FOR SELECT TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: machine_models update_model; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY update_model ON public.machine_models FOR UPDATE USING ((( SELECT auth.uid() AS uid) = owner_id)) WITH CHECK ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: embedded update_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY update_policy ON public.embedded FOR UPDATE TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id)) WITH CHECK ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: machine_coils update_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY update_policy ON public.machine_coils FOR UPDATE TO authenticated USING ((EXISTS ( SELECT 1
   FROM public.machines m
  WHERE ((m.id = machine_coils.machine_id) AND (m.owner_id = auth.uid()))))) WITH CHECK ((EXISTS ( SELECT 1
   FROM public.machines m
  WHERE ((m.id = machine_coils.machine_id) AND (m.owner_id = auth.uid())))));


--
-- Name: machines update_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY update_policy ON public.machines FOR UPDATE TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id)) WITH CHECK ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: model_coils update_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY update_policy ON public.model_coils FOR UPDATE USING ((EXISTS ( SELECT 1
   FROM public.machine_models m
  WHERE ((m.id = model_coils.model_id) AND (m.owner_id = auth.uid()))))) WITH CHECK ((EXISTS ( SELECT 1
   FROM public.machine_models m
  WHERE ((m.id = model_coils.model_id) AND (m.owner_id = auth.uid())))));


--
-- Name: products update_policy; Type: POLICY; Schema: public; Owner: -
--

CREATE POLICY update_policy ON public.products FOR UPDATE TO authenticated USING ((( SELECT auth.uid() AS uid) = owner_id)) WITH CHECK ((( SELECT auth.uid() AS uid) = owner_id));


--
-- Name: SCHEMA public; Type: ACL; Schema: -; Owner: -
--

GRANT USAGE ON SCHEMA public TO postgres;
GRANT USAGE ON SCHEMA public TO anon;
GRANT USAGE ON SCHEMA public TO authenticated;
GRANT USAGE ON SCHEMA public TO service_role;


--
-- Name: FUNCTION bind_embedded_machine(embedded_id_ uuid, machine_id_ uuid); Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON FUNCTION public.bind_embedded_machine(embedded_id_ uuid, machine_id_ uuid) TO anon;
GRANT ALL ON FUNCTION public.bind_embedded_machine(embedded_id_ uuid, machine_id_ uuid) TO authenticated;
GRANT ALL ON FUNCTION public.bind_embedded_machine(embedded_id_ uuid, machine_id_ uuid) TO service_role;


--
-- Name: FUNCTION debit_product_stock_on_coil_refill(); Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON FUNCTION public.debit_product_stock_on_coil_refill() TO postgres;
GRANT ALL ON FUNCTION public.debit_product_stock_on_coil_refill() TO anon;
GRANT ALL ON FUNCTION public.debit_product_stock_on_coil_refill() TO authenticated;
GRANT ALL ON FUNCTION public.debit_product_stock_on_coil_refill() TO service_role;


--
-- Name: FUNCTION fill_sale_coil_data(); Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON FUNCTION public.fill_sale_coil_data() TO postgres;
GRANT ALL ON FUNCTION public.fill_sale_coil_data() TO anon;
GRANT ALL ON FUNCTION public.fill_sale_coil_data() TO authenticated;
GRANT ALL ON FUNCTION public.fill_sale_coil_data() TO service_role;


--
-- Name: FUNCTION get_machine_name(machine_id uuid); Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON FUNCTION public.get_machine_name(machine_id uuid) TO postgres;
GRANT ALL ON FUNCTION public.get_machine_name(machine_id uuid) TO anon;
GRANT ALL ON FUNCTION public.get_machine_name(machine_id uuid) TO authenticated;
GRANT ALL ON FUNCTION public.get_machine_name(machine_id uuid) TO service_role;


--
-- Name: FUNCTION sync_machine_coils_on_model_change(); Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON FUNCTION public.sync_machine_coils_on_model_change() TO postgres;
GRANT ALL ON FUNCTION public.sync_machine_coils_on_model_change() TO anon;
GRANT ALL ON FUNCTION public.sync_machine_coils_on_model_change() TO authenticated;
GRANT ALL ON FUNCTION public.sync_machine_coils_on_model_change() TO service_role;


--
-- Name: TABLE credentials; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.credentials TO postgres;
GRANT ALL ON TABLE public.credentials TO anon;
GRANT ALL ON TABLE public.credentials TO authenticated;
GRANT ALL ON TABLE public.credentials TO service_role;


--
-- Name: TABLE embedded; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.embedded TO postgres;
GRANT ALL ON TABLE public.embedded TO anon;
GRANT ALL ON TABLE public.embedded TO authenticated;
GRANT ALL ON TABLE public.embedded TO service_role;


--
-- Name: SEQUENCE embeddeds_subdomain_seq; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON SEQUENCE public.embeddeds_subdomain_seq TO postgres;
GRANT ALL ON SEQUENCE public.embeddeds_subdomain_seq TO anon;
GRANT ALL ON SEQUENCE public.embeddeds_subdomain_seq TO authenticated;
GRANT ALL ON SEQUENCE public.embeddeds_subdomain_seq TO service_role;


--
-- Name: TABLE machine_coils; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.machine_coils TO postgres;
GRANT ALL ON TABLE public.machine_coils TO anon;
GRANT ALL ON TABLE public.machine_coils TO authenticated;
GRANT ALL ON TABLE public.machine_coils TO service_role;


--
-- Name: TABLE machine_models; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.machine_models TO postgres;
GRANT ALL ON TABLE public.machine_models TO anon;
GRANT ALL ON TABLE public.machine_models TO authenticated;
GRANT ALL ON TABLE public.machine_models TO service_role;


--
-- Name: TABLE machines; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.machines TO postgres;
GRANT ALL ON TABLE public.machines TO anon;
GRANT ALL ON TABLE public.machines TO authenticated;
GRANT ALL ON TABLE public.machines TO service_role;


--
-- Name: TABLE metrics; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.metrics TO postgres;
GRANT ALL ON TABLE public.metrics TO anon;
GRANT ALL ON TABLE public.metrics TO authenticated;
GRANT ALL ON TABLE public.metrics TO service_role;


--
-- Name: TABLE metrics_2026_2036; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.metrics_2026_2036 TO postgres;
GRANT ALL ON TABLE public.metrics_2026_2036 TO anon;
GRANT ALL ON TABLE public.metrics_2026_2036 TO authenticated;
GRANT ALL ON TABLE public.metrics_2026_2036 TO service_role;


--
-- Name: TABLE model_coils; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.model_coils TO postgres;
GRANT ALL ON TABLE public.model_coils TO anon;
GRANT ALL ON TABLE public.model_coils TO authenticated;
GRANT ALL ON TABLE public.model_coils TO service_role;


--
-- Name: TABLE payments; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.payments TO postgres;
GRANT ALL ON TABLE public.payments TO anon;
GRANT ALL ON TABLE public.payments TO authenticated;
GRANT ALL ON TABLE public.payments TO service_role;


--
-- Name: TABLE products; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.products TO postgres;
GRANT ALL ON TABLE public.products TO anon;
GRANT ALL ON TABLE public.products TO authenticated;
GRANT ALL ON TABLE public.products TO service_role;


--
-- Name: TABLE sales; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.sales TO postgres;
GRANT ALL ON TABLE public.sales TO anon;
GRANT ALL ON TABLE public.sales TO authenticated;
GRANT ALL ON TABLE public.sales TO service_role;


--
-- Name: TABLE sales_metrics_v1; Type: ACL; Schema: public; Owner: -
--

GRANT ALL ON TABLE public.sales_metrics_v1 TO anon;
GRANT ALL ON TABLE public.sales_metrics_v1 TO authenticated;
GRANT ALL ON TABLE public.sales_metrics_v1 TO service_role;


--
-- Name: DEFAULT PRIVILEGES FOR SEQUENCES; Type: DEFAULT ACL; Schema: public; Owner: -
--

ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON SEQUENCES  TO postgres;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON SEQUENCES  TO anon;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON SEQUENCES  TO authenticated;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON SEQUENCES  TO service_role;


--
-- Name: DEFAULT PRIVILEGES FOR SEQUENCES; Type: DEFAULT ACL; Schema: public; Owner: -
--

ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON SEQUENCES  TO postgres;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON SEQUENCES  TO anon;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON SEQUENCES  TO authenticated;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON SEQUENCES  TO service_role;


--
-- Name: DEFAULT PRIVILEGES FOR FUNCTIONS; Type: DEFAULT ACL; Schema: public; Owner: -
--

ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON FUNCTIONS  TO postgres;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON FUNCTIONS  TO anon;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON FUNCTIONS  TO authenticated;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON FUNCTIONS  TO service_role;


--
-- Name: DEFAULT PRIVILEGES FOR FUNCTIONS; Type: DEFAULT ACL; Schema: public; Owner: -
--

ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON FUNCTIONS  TO postgres;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON FUNCTIONS  TO anon;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON FUNCTIONS  TO authenticated;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON FUNCTIONS  TO service_role;


--
-- Name: DEFAULT PRIVILEGES FOR TABLES; Type: DEFAULT ACL; Schema: public; Owner: -
--

ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON TABLES  TO postgres;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON TABLES  TO anon;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON TABLES  TO authenticated;
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT ALL ON TABLES  TO service_role;


--
-- Name: DEFAULT PRIVILEGES FOR TABLES; Type: DEFAULT ACL; Schema: public; Owner: -
--

ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON TABLES  TO postgres;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON TABLES  TO anon;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON TABLES  TO authenticated;
ALTER DEFAULT PRIVILEGES FOR ROLE supabase_admin IN SCHEMA public GRANT ALL ON TABLES  TO service_role;


--
-- PostgreSQL database dump complete
--


