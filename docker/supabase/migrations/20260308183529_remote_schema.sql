


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


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_3da48be8_f8f8_42c0_8d49_fce78954815f"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_3da48be8_f8f8_42c0_8d49_fce78954815f', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_3da48be8_f8f8_42c0_8d49_fce78954815f"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_4ae3302c_f48f_4dd1_b5b7_2550736f387f"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_4ae3302c_f48f_4dd1_b5b7_2550736f387f', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_4ae3302c_f48f_4dd1_b5b7_2550736f387f"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_51edbab4_6b36_4559_b8e7_2e32605638d2"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_51edbab4_6b36_4559_b8e7_2e32605638d2', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_51edbab4_6b36_4559_b8e7_2e32605638d2"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_561d4e9e_b4e3_4e1d_90d7_7d6a1df03b9f"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_561d4e9e_b4e3_4e1d_90d7_7d6a1df03b9f', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_561d4e9e_b4e3_4e1d_90d7_7d6a1df03b9f"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_8f379ccd_a27c_474b_9e6c_6181d2723314"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_8f379ccd_a27c_474b_9e6c_6181d2723314', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_8f379ccd_a27c_474b_9e6c_6181d2723314"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_98f05dd0_9a57_436f_ba80_7e0c83158b5a"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_98f05dd0_9a57_436f_ba80_7e0c83158b5a', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_98f05dd0_9a57_436f_ba80_7e0c83158b5a"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_ccd0ec78_6984_40d0_86fd_38388a729dee', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$ begin perform pg_notify('n8n_channel_ccf0f211_2d2e_4889_97ae_7e172d45dba8', row_to_json(new)::text); return null; end; $$;


ALTER FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() OWNER TO "postgres";

SET default_tablespace = '';

SET default_table_access_method = "heap";


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
    "alias" smallint,
    "capacity" smallint DEFAULT '0'::smallint NOT NULL
);


ALTER TABLE "public"."machine_coils" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."machine_models" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL
);


ALTER TABLE "public"."machine_models" OWNER TO "supabase_admin";


CREATE TABLE IF NOT EXISTS "public"."machines" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "owner_id" "uuid" DEFAULT "auth"."uid"(),
    "serial_number" "text",
    "name" "text",
    "refilled_at" timestamp with time zone DEFAULT ("now"() AT TIME ZONE 'utc'::"text") NOT NULL
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
    "model_id" "uuid" DEFAULT "gen_random_uuid"(),
    "alias" smallint,
    "item_number" smallint
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
    "owner_id" "uuid" DEFAULT "auth"."uid"() NOT NULL
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
    "lng" double precision
);


ALTER TABLE "public"."sales" OWNER TO "supabase_admin";


ALTER TABLE ONLY "public"."metrics" ATTACH PARTITION "public"."metrics_2026_2036" FOR VALUES FROM ('2026-01-01 00:00:00+00') TO ('2036-01-01 00:00:00+00');



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



CREATE OR REPLACE TRIGGER "n8n_trigger_4ae3302c_f48f_4dd1_b5b7_2550736f387f" AFTER INSERT ON "public"."sales" FOR EACH ROW EXECUTE FUNCTION "public"."n8n_trigger_function_4ae3302c_f48f_4dd1_b5b7_2550736f387f"();



CREATE OR REPLACE TRIGGER "n8n_trigger_8f379ccd_a27c_474b_9e6c_6181d2723314" AFTER INSERT ON "public"."embedded" FOR EACH ROW EXECUTE FUNCTION "public"."n8n_trigger_function_8f379ccd_a27c_474b_9e6c_6181d2723314"();



CREATE OR REPLACE TRIGGER "n8n_trigger_98f05dd0_9a57_436f_ba80_7e0c83158b5a" AFTER INSERT ON "public"."sales" FOR EACH ROW EXECUTE FUNCTION "public"."n8n_trigger_function_98f05dd0_9a57_436f_ba80_7e0c83158b5a"();



CREATE OR REPLACE TRIGGER "n8n_trigger_ccf0f211_2d2e_4889_97ae_7e172d45dba8" AFTER INSERT ON "public"."sales" FOR EACH ROW EXECUTE FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"();



ALTER TABLE ONLY "public"."embedded"
    ADD CONSTRAINT "embedded_machine_id_fkey" FOREIGN KEY ("machine_id") REFERENCES "public"."machines"("id");



ALTER TABLE ONLY "public"."model_coils"
    ADD CONSTRAINT "model_coils_model_id_fkey" FOREIGN KEY ("model_id") REFERENCES "public"."machine_models"("id");



ALTER TABLE ONLY "public"."sales"
    ADD CONSTRAINT "sale_machine_id_fkey" FOREIGN KEY ("machine_id") REFERENCES "public"."machines"("id");



ALTER TABLE ONLY "public"."sales"
    ADD CONSTRAINT "sale_product_id_fkey" FOREIGN KEY ("product_id") REFERENCES "public"."products"("id");



ALTER TABLE ONLY "public"."sales"
    ADD CONSTRAINT "sales_embedded_id_fkey" FOREIGN KEY ("embedded_id") REFERENCES "public"."embedded"("id");



ALTER TABLE "public"."embedded" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "insert_policy" ON "public"."embedded" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."machines" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."products" FOR INSERT TO "authenticated" WITH CHECK (true);



CREATE POLICY "insert_policy" ON "public"."sales" FOR INSERT TO "authenticated" WITH CHECK (true);



ALTER TABLE "public"."machine_coils" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."machine_models" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."machines" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."model_coils" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."payments" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."products" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."sales" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "select_policy" ON "public"."embedded" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "select_policy" ON "public"."machines" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "select_policy" ON "public"."products" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "select_policy" ON "public"."sales" FOR SELECT TO "authenticated" USING ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));



CREATE POLICY "update_policy" ON "public"."machines" FOR UPDATE TO "authenticated" USING (true) WITH CHECK ((( SELECT "auth"."uid"() AS "uid") = "owner_id"));





ALTER PUBLICATION "supabase_realtime" OWNER TO "postgres";


ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."sales";






GRANT USAGE ON SCHEMA "public" TO "postgres";
GRANT USAGE ON SCHEMA "public" TO "anon";
GRANT USAGE ON SCHEMA "public" TO "authenticated";
GRANT USAGE ON SCHEMA "public" TO "service_role";











































































































































































GRANT ALL ON FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_153d7c01_040f_4ccf_b0e4_81c78f7f5e4c"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_3da48be8_f8f8_42c0_8d49_fce78954815f"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_3da48be8_f8f8_42c0_8d49_fce78954815f"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_3da48be8_f8f8_42c0_8d49_fce78954815f"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_4ae3302c_f48f_4dd1_b5b7_2550736f387f"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_4ae3302c_f48f_4dd1_b5b7_2550736f387f"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_4ae3302c_f48f_4dd1_b5b7_2550736f387f"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_51edbab4_6b36_4559_b8e7_2e32605638d2"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_51edbab4_6b36_4559_b8e7_2e32605638d2"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_51edbab4_6b36_4559_b8e7_2e32605638d2"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_561d4e9e_b4e3_4e1d_90d7_7d6a1df03b9f"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_561d4e9e_b4e3_4e1d_90d7_7d6a1df03b9f"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_561d4e9e_b4e3_4e1d_90d7_7d6a1df03b9f"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_8f379ccd_a27c_474b_9e6c_6181d2723314"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_8f379ccd_a27c_474b_9e6c_6181d2723314"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_8f379ccd_a27c_474b_9e6c_6181d2723314"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_98f05dd0_9a57_436f_ba80_7e0c83158b5a"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_98f05dd0_9a57_436f_ba80_7e0c83158b5a"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_98f05dd0_9a57_436f_ba80_7e0c83158b5a"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccd0ec78_6984_40d0_86fd_38388a729dee"() TO "service_role";



GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() TO "anon";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."n8n_trigger_function_ccf0f211_2d2e_4889_97ae_7e172d45dba8"() TO "service_role";


















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































