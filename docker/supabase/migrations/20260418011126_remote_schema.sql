


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


CREATE EXTENSION IF NOT EXISTS "pg_net" WITH SCHEMA "extensions";






COMMENT ON SCHEMA "public" IS 'standard public schema';



CREATE EXTENSION IF NOT EXISTS "pg_graphql" WITH SCHEMA "graphql";






CREATE EXTENSION IF NOT EXISTS "pg_stat_statements" WITH SCHEMA "extensions";






CREATE EXTENSION IF NOT EXISTS "pgcrypto" WITH SCHEMA "extensions";






CREATE EXTENSION IF NOT EXISTS "pgjwt" WITH SCHEMA "extensions";






CREATE EXTENSION IF NOT EXISTS "supabase_vault" WITH SCHEMA "vault";






CREATE EXTENSION IF NOT EXISTS "uuid-ossp" WITH SCHEMA "extensions";






CREATE TYPE "public"."embedded_status" AS ENUM (
    'online',
    'offline'
);


ALTER TYPE "public"."embedded_status" OWNER TO "supabase_admin";


CREATE TYPE "public"."machine_category" AS ENUM (
    'snack',
    'drink',
    'frozen',
    'candy',
    'personal_care',
    'other'
);


ALTER TYPE "public"."machine_category" OWNER TO "supabase_admin";


CREATE TYPE "public"."metric_name" AS ENUM (
    'paxcounter'
);


ALTER TYPE "public"."metric_name" OWNER TO "supabase_admin";


CREATE TYPE "public"."metric_unit" AS ENUM (
    '-'
);


ALTER TYPE "public"."metric_unit" OWNER TO "supabase_admin";


CREATE TYPE "public"."sale_channel" AS ENUM (
    'ble',
    'mqtt',
    'cash'
);


ALTER TYPE "public"."sale_channel" OWNER TO "supabase_admin";


CREATE OR REPLACE FUNCTION "public"."bind_embedded_machine"("embedded_id_" "uuid", "machine_id_" "uuid") RETURNS "void"
    LANGUAGE "plpgsql"
    AS $$
begin
	update embedded set machine_id = null where machine_id = machine_id_;
	update embedded set machine_id = machine_id_ where id = embedded_id_;
end;
$$;


ALTER FUNCTION "public"."bind_embedded_machine"("embedded_id_" "uuid", "machine_id_" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."debit_product_stock_on_coil_refill"() RETURNS "trigger"
    LANGUAGE "plpgsql"
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


ALTER FUNCTION "public"."debit_product_stock_on_coil_refill"() OWNER TO "supabase_admin";


CREATE OR REPLACE FUNCTION "public"."fill_sale_coil_data"() RETURNS "trigger"
    LANGUAGE "plpgsql"
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


ALTER FUNCTION "public"."fill_sale_coil_data"() OWNER TO "supabase_admin";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_ccd0ec78_6984_40d0_86fd_38388a729dee', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_ccf0f211_2d2e_4889_97ae_7e172d45dba8', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."sync_machine_coils_on_model_change"() RETURNS "trigger"
    LANGUAGE "plpgsql"
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


ALTER FUNCTION "public"."sync_machine_coils_on_model_change"() OWNER TO "supabase_admin";

SET default_tablespace = '';

SET default_table_access_method = "heap";


CREATE TABLE IF NOT EXISTS "public"."credentials" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "owner_id" "uuid" DEFAULT "auth"."uid"() NOT NULL,
    "key" "text" NOT NULL,
    "value" "text" NOT NULL
);


ALTER TABLE "public"."credentials" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."embedded" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "owner_id" "uuid" DEFAULT "auth"."uid"(),
    "machine_id" "uuid",
    "subdomain" bigint NOT NULL,
    "mac_address" "text",
    "status" "public"."embedded_status" DEFAULT 'offline'::"public"."embedded_status" NOT NULL,
    "status_at" timestamp with time zone DEFAULT ("now"() AT TIME ZONE 'utc'::"text") NOT NULL,
    "passkey" "text" DEFAULT SUBSTRING("md5"(("random"())::"text") FROM 1 FOR 18) NOT NULL
);


ALTER TABLE "public"."embedded" OWNER TO "supabase_admin";


ALTER TABLE "public"."embedded" ALTER COLUMN "subdomain" ADD GENERATED BY DEFAULT AS IDENTITY (
    SEQUENCE NAME "public"."embeddeds_subdomain_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);



CREATE TABLE IF NOT EXISTS "public"."machine_coils" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "product_id" "uuid",
    "item_number" smallint,
    "alias" "text",
    "capacity" integer,
    "machine_id" "uuid",
    "item_price" real DEFAULT '0'::real NOT NULL,
    "current_stock" integer DEFAULT 0 NOT NULL
);


ALTER TABLE "public"."machine_coils" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."machine_models" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "name" "text",
    "manufacturer" "text",
    "owner_id" "uuid" DEFAULT "auth"."uid"(),
    "enabled" boolean DEFAULT true NOT NULL
);


ALTER TABLE "public"."machine_models" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."machines" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "owner_id" "uuid" DEFAULT "auth"."uid"(),
    "serial_number" "text",
    "name" "text",
    "refilled_at" timestamp with time zone DEFAULT ("now"() AT TIME ZONE 'utc'::"text") NOT NULL,
    "model_id" "uuid",
    "lat" double precision,
    "lng" double precision,
    "monthly_rent" real DEFAULT '0'::real,
    "category" "public"."machine_category" DEFAULT 'other'::"public"."machine_category",
    "location_name" "text",
    "address" "text"
);


ALTER TABLE "public"."machines" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."metrics" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "machine_id" "uuid",
    "name" "public"."metric_name",
    "unit" "public"."metric_unit" DEFAULT '-'::"public"."metric_unit",
    "value" real,
    "payload" "jsonb",
    "embedded_id" "uuid"
)
PARTITION BY RANGE ("created_at");


ALTER TABLE "public"."metrics" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."metrics_2026_2036" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "machine_id" "uuid",
    "name" "public"."metric_name",
    "unit" "public"."metric_unit" DEFAULT '-'::"public"."metric_unit",
    "value" real,
    "payload" "jsonb",
    "embedded_id" "uuid"
);


ALTER TABLE "public"."metrics_2026_2036" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."model_coils" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "alias" "text",
    "item_number" smallint,
    "capacity" smallint,
    "model_id" "uuid"
);


ALTER TABLE "public"."model_coils" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."payments" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL
);


ALTER TABLE "public"."payments" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."products" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "name" "text",
    "barcode" "text",
    "owner_id" "uuid" DEFAULT "auth"."uid"() NOT NULL,
    "image_url" "text",
    "price" real,
    "enabled" boolean DEFAULT true NOT NULL,
    "current_stock" integer DEFAULT 0 NOT NULL
);


ALTER TABLE "public"."products" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."sales" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "embedded_id" "uuid" NOT NULL,
    "machine_id" "uuid",
    "product_id" "uuid",
    "item_price" double precision DEFAULT '0'::double precision NOT NULL,
    "item_number" bigint DEFAULT '0'::bigint NOT NULL,
    "channel" "public"."sale_channel" NOT NULL,
    "owner_id" "uuid" DEFAULT "auth"."uid"(),
    "lat" double precision,
    "lng" double precision,
    "coil_alias" "text"
);


ALTER TABLE "public"."sales" OWNER TO "supabase_admin";


CREATE OR REPLACE VIEW "public"."sales_metrics_v1" AS
 SELECT "s"."machine_id",
    "date_trunc"('hour'::"text", "s"."created_at") AS "created_at",
    "count"(*) AS "total"
   FROM "public"."sales" "s"
  GROUP BY "s"."machine_id", ("date_trunc"('hour'::"text", "s"."created_at"));


ALTER VIEW "public"."sales_metrics_v1" OWNER TO "postgres";


ALTER TABLE ONLY "public"."metrics" ATTACH PARTITION "public"."metrics_2026_2036" FOR VALUES FROM ('2026-01-01 00:00:00+00') TO ('2036-01-01 00:00:00+00');



ALTER TABLE ONLY "public"."credentials"
    ADD CONSTRAINT "credentials_owner_id_key_key" UNIQUE ("owner_id", "key");



ALTER TABLE ONLY "public"."credentials"
    ADD CONSTRAINT "credentials_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."embedded"
    ADD CONSTRAINT "embedded_machine_id_key" UNIQUE ("machine_id");



ALTER TABLE ONLY "public"."embedded"
    ADD CONSTRAINT "embeddeds_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."machine_coils"
    ADD CONSTRAINT "machine_coils_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."machine_models"
    ADD CONSTRAINT "machine_models_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."machines"
    ADD CONSTRAINT "machine_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."model_coils"
    ADD CONSTRAINT "model_coils_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."payments"
    ADD CONSTRAINT "payments_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."products"
    ADD CONSTRAINT "product_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."sales"
    ADD CONSTRAINT "sale_pkey" PRIMARY KEY ("id");



CREATE OR REPLACE TRIGGER "n8n_trigger_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c" AFTER INSERT ON "public"."embedded" FOR EACH ROW EXECUTE FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"();



CREATE OR REPLACE TRIGGER "n8n_trigger_ccf0f211_2d2e_4889_97ae_7e172d45dba8" AFTER INSERT ON "public"."sales" FOR EACH ROW EXECUTE FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"();



CREATE OR REPLACE TRIGGER "trg_debit_product_stock_on_coil_refill" AFTER UPDATE OF "current_stock" ON "public"."machine_coils" FOR EACH ROW EXECUTE FUNCTION "public"."debit_product_stock_on_coil_refill"();



CREATE OR REPLACE TRIGGER "trg_fill_sale_coil_data" BEFORE INSERT ON "public"."sales" FOR EACH ROW EXECUTE FUNCTION "public"."fill_sale_coil_data"();



CREATE OR REPLACE TRIGGER "trg_sync_machine_coils" AFTER UPDATE OF "model_id" ON "public"."machines" FOR EACH ROW EXECUTE FUNCTION "public"."sync_machine_coils_on_model_change"();



ALTER TABLE ONLY "public"."embedded"
    ADD CONSTRAINT "embedded_machine_id_fkey" FOREIGN KEY ("machine_id") REFERENCES "public"."machines"("id");



ALTER TABLE ONLY "public"."machine_coils"
    ADD CONSTRAINT "fk_machine_coils_machine" FOREIGN KEY ("machine_id") REFERENCES "public"."machines"("id");



ALTER TABLE ONLY "public"."machine_coils"
    ADD CONSTRAINT "fk_machine_coils_product" FOREIGN KEY ("product_id") REFERENCES "public"."products"("id");



ALTER TABLE ONLY "public"."machines"
    ADD CONSTRAINT "machines_model_id_fkey" FOREIGN KEY ("model_id") REFERENCES "public"."machine_models"("id");



ALTER TABLE ONLY "public"."model_coils"
    ADD CONSTRAINT "model_coils_model_id_fkey" FOREIGN KEY ("model_id") REFERENCES "public"."machine_models"("id");



ALTER TABLE ONLY "public"."sales"
    ADD CONSTRAINT "sale_machine_id_fkey" FOREIGN KEY ("machine_id") REFERENCES "public"."machines"("id");



ALTER TABLE ONLY "public"."sales"
    ADD CONSTRAINT "sale_product_id_fkey" FOREIGN KEY ("product_id") REFERENCES "public"."products"("id");



ALTER TABLE ONLY "public"."sales"
    ADD CONSTRAINT "sales_embedded_id_fkey" FOREIGN KEY ("embedded_id") REFERENCES "public"."embedded"("id");



ALTER TABLE "public"."credentials" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "credentials_delete" ON "public"."credentials" FOR DELETE TO "authenticated" USING (("owner_id" = "auth"."uid"()));



CREATE POLICY "credentials_insert" ON "public"."credentials" FOR INSERT TO "authenticated" WITH CHECK (("owner_id" = "auth"."uid"()));



CREATE POLICY "credentials_select" ON "public"."credentials" FOR SELECT TO "authenticated" USING (("owner_id" = "auth"."uid"()));



CREATE POLICY "credentials_update" ON "public"."credentials" FOR UPDATE TO "authenticated" USING (("owner_id" = "auth"."uid"())) WITH CHECK (("owner_id" = "auth"."uid"()));



ALTER TABLE "public"."embedded" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "insert_policy" ON "public"."embedded" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."machine_models" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."machines" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."model_coils" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."products" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."sales" FOR INSERT TO "authenticated" WITH CHECK (true);



ALTER TABLE "public"."machine_coils" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."machine_models" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."machines" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."metrics" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."model_coils" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."payments" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "policy_insert" ON "public"."machine_coils" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "policy_select" ON "public"."metrics" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."embedded" "e"
  WHERE (("e"."id" = "metrics"."embedded_id") AND ("e"."owner_id" = "auth"."uid"())))));



ALTER TABLE "public"."products" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."sales" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "select_policy" ON "public"."embedded" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "select_policy" ON "public"."machine_coils" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."machines" "m"
  WHERE (("m"."id" = "machine_coils"."machine_id") AND ("m"."owner_id" = "auth"."uid"())))));



CREATE POLICY "select_policy" ON "public"."machine_models" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "select_policy" ON "public"."machines" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "select_policy" ON "public"."model_coils" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."machine_models" "m"
  WHERE (("m"."id" = "model_coils"."model_id") AND ("m"."owner_id" = "auth"."uid"())))));



CREATE POLICY "select_policy" ON "public"."products" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "select_policy" ON "public"."sales" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "update_model" ON "public"."machine_models" FOR UPDATE USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id")) WITH CHECK ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "update_policy" ON "public"."embedded" FOR UPDATE TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id")) WITH CHECK ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "update_policy" ON "public"."machine_coils" FOR UPDATE TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."machines" "m"
  WHERE (("m"."id" = "machine_coils"."machine_id") AND ("m"."owner_id" = "auth"."uid"()))))) WITH CHECK ((EXISTS ( SELECT 1
   FROM "public"."machines" "m"
  WHERE (("m"."id" = "machine_coils"."machine_id") AND ("m"."owner_id" = "auth"."uid"())))));



CREATE POLICY "update_policy" ON "public"."machines" FOR UPDATE TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id")) WITH CHECK ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "update_policy" ON "public"."model_coils" FOR UPDATE USING ((EXISTS ( SELECT 1
   FROM "public"."machine_models" "m"
  WHERE (("m"."id" = "model_coils"."model_id") AND ("m"."owner_id" = "auth"."uid"()))))) WITH CHECK ((EXISTS ( SELECT 1
   FROM "public"."machine_models" "m"
  WHERE (("m"."id" = "model_coils"."model_id") AND ("m"."owner_id" = "auth"."uid"())))));



CREATE POLICY "update_policy" ON "public"."products" FOR UPDATE TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id")) WITH CHECK ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));





ALTER PUBLICATION "supabase_realtime" OWNER TO "postgres";


ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."embedded";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."machine_coils";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."sales";






GRANT USAGE ON SCHEMA "public" TO "postgres";
GRANT USAGE ON SCHEMA "public" TO "anon";
GRANT USAGE ON SCHEMA "public" TO "authenticated";
GRANT USAGE ON SCHEMA "public" TO "service_role";











































































































































































GRANT ALL ON FUNCTION "public"."bind_embedded_machine"("embedded_id_" "uuid", "machine_id_" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."bind_embedded_machine"("embedded_id_" "uuid", "machine_id_" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."bind_embedded_machine"("embedded_id_" "uuid", "machine_id_" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."debit_product_stock_on_coil_refill"() TO "postgres";
GRANT ALL ON FUNCTION "public"."debit_product_stock_on_coil_refill"() TO "anon";
GRANT ALL ON FUNCTION "public"."debit_product_stock_on_coil_refill"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."debit_product_stock_on_coil_refill"() TO "service_role";



GRANT ALL ON FUNCTION "public"."fill_sale_coil_data"() TO "postgres";
GRANT ALL ON FUNCTION "public"."fill_sale_coil_data"() TO "anon";
GRANT ALL ON FUNCTION "public"."fill_sale_coil_data"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."fill_sale_coil_data"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() TO "service_role";



GRANT ALL ON FUNCTION "public"."sync_machine_coils_on_model_change"() TO "postgres";
GRANT ALL ON FUNCTION "public"."sync_machine_coils_on_model_change"() TO "anon";
GRANT ALL ON FUNCTION "public"."sync_machine_coils_on_model_change"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."sync_machine_coils_on_model_change"() TO "service_role";


















GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."credentials" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."credentials" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."credentials" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."credentials" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."embedded" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."embedded" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."embedded" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."embedded" TO "service_role";



GRANT ALL ON SEQUENCE "public"."embeddeds_subdomain_seq" TO "postgres";
GRANT ALL ON SEQUENCE "public"."embeddeds_subdomain_seq" TO "anon";
GRANT ALL ON SEQUENCE "public"."embeddeds_subdomain_seq" TO "authenticated";
GRANT ALL ON SEQUENCE "public"."embeddeds_subdomain_seq" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_coils" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_coils" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_coils" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_coils" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_models" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_models" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_models" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machine_models" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machines" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machines" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machines" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."machines" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics_2026_2036" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics_2026_2036" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics_2026_2036" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."metrics_2026_2036" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."model_coils" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."model_coils" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."model_coils" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."model_coils" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."payments" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."payments" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."payments" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."payments" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."products" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."products" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."products" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."products" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."sales" TO "postgres";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."sales" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."sales" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."sales" TO "service_role";



GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."sales_metrics_v1" TO "anon";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."sales_metrics_v1" TO "authenticated";
GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLE "public"."sales_metrics_v1" TO "service_role";









ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "postgres";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "anon";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "authenticated";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "service_role";






ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "postgres";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "anon";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "authenticated";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "service_role";






ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLES TO "postgres";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLES TO "anon";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLES TO "authenticated";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT SELECT,INSERT,REFERENCES,DELETE,TRIGGER,TRUNCATE,UPDATE ON TABLES TO "service_role";































