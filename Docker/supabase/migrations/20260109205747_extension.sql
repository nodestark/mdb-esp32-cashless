
  create table "public"."companies" (
    "id" uuid not null default gen_random_uuid(),
    "created_at" timestamp with time zone not null default now(),
    "name" text
      );


alter table "public"."companies" enable row level security;


  create table "public"."product_category" (
    "created_at" timestamp with time zone not null default now(),
    "name" text,
    "company" uuid default gen_random_uuid(),
    "id" uuid not null default gen_random_uuid()
      );


alter table "public"."product_category" enable row level security;


  create table "public"."products" (
    "created_at" timestamp with time zone not null default now(),
    "name" text,
    "sellprice" numeric,
    "description" text,
    "company" uuid default gen_random_uuid(),
    "category" uuid default gen_random_uuid(),
    "id" uuid not null default gen_random_uuid()
      );


alter table "public"."products" enable row level security;


  create table "public"."users" (
    "id" uuid not null default auth.uid(),
    "created_at" timestamp with time zone not null default now(),
    "username" text,
    "company" uuid default gen_random_uuid()
      );


alter table "public"."users" enable row level security;


  create table "public"."vendingMachine" (
    "id" uuid not null default gen_random_uuid(),
    "created_at" timestamp with time zone not null default now(),
    "name" text,
    "location_lat" real,
    "location_lon" real,
    "company" uuid default gen_random_uuid(),
    "embedded" uuid default gen_random_uuid()
      );


alter table "public"."vendingMachine" enable row level security;

alter table "public"."embeddeds" add column "company" uuid;

CREATE UNIQUE INDEX companies_pkey ON public.companies USING btree (id);

CREATE UNIQUE INDEX product_category_pkey ON public.product_category USING btree (id);

CREATE UNIQUE INDEX products_pkey ON public.products USING btree (id);

CREATE UNIQUE INDEX users_pkey ON public.users USING btree (id);

CREATE UNIQUE INDEX "vendingMachine_pkey" ON public."vendingMachine" USING btree (id);

alter table "public"."companies" add constraint "companies_pkey" PRIMARY KEY using index "companies_pkey";

alter table "public"."product_category" add constraint "product_category_pkey" PRIMARY KEY using index "product_category_pkey";

alter table "public"."products" add constraint "products_pkey" PRIMARY KEY using index "products_pkey";

alter table "public"."users" add constraint "users_pkey" PRIMARY KEY using index "users_pkey";

alter table "public"."vendingMachine" add constraint "vendingMachine_pkey" PRIMARY KEY using index "vendingMachine_pkey";

alter table "public"."embeddeds" add constraint "embeddeds_company_fkey" FOREIGN KEY (company) REFERENCES public.companies(id) not valid;

alter table "public"."embeddeds" validate constraint "embeddeds_company_fkey";

alter table "public"."product_category" add constraint "product_category_company_fkey" FOREIGN KEY (company) REFERENCES public.companies(id) not valid;

alter table "public"."product_category" validate constraint "product_category_company_fkey";

alter table "public"."products" add constraint "products_company_fkey" FOREIGN KEY (company) REFERENCES public.companies(id) not valid;

alter table "public"."products" validate constraint "products_company_fkey";

alter table "public"."products" add constraint "products_id_fkey" FOREIGN KEY (id) REFERENCES public.product_category(id) not valid;

alter table "public"."products" validate constraint "products_id_fkey";

alter table "public"."users" add constraint "users_company_fkey" FOREIGN KEY (company) REFERENCES public.companies(id) not valid;

alter table "public"."users" validate constraint "users_company_fkey";

alter table "public"."vendingMachine" add constraint "vendingMachine_company_fkey" FOREIGN KEY (company) REFERENCES public.companies(id) not valid;

alter table "public"."vendingMachine" validate constraint "vendingMachine_company_fkey";

alter table "public"."vendingMachine" add constraint "vendingMachine_embedded_fkey" FOREIGN KEY (embedded) REFERENCES public.embeddeds(id) not valid;

alter table "public"."vendingMachine" validate constraint "vendingMachine_embedded_fkey";

grant delete on table "public"."companies" to "anon";

grant insert on table "public"."companies" to "anon";

grant references on table "public"."companies" to "anon";

grant select on table "public"."companies" to "anon";

grant trigger on table "public"."companies" to "anon";

grant truncate on table "public"."companies" to "anon";

grant update on table "public"."companies" to "anon";

grant delete on table "public"."companies" to "authenticated";

grant insert on table "public"."companies" to "authenticated";

grant references on table "public"."companies" to "authenticated";

grant select on table "public"."companies" to "authenticated";

grant trigger on table "public"."companies" to "authenticated";

grant truncate on table "public"."companies" to "authenticated";

grant update on table "public"."companies" to "authenticated";

grant delete on table "public"."companies" to "postgres";

grant insert on table "public"."companies" to "postgres";

grant references on table "public"."companies" to "postgres";

grant select on table "public"."companies" to "postgres";

grant trigger on table "public"."companies" to "postgres";

grant truncate on table "public"."companies" to "postgres";

grant update on table "public"."companies" to "postgres";

grant delete on table "public"."companies" to "service_role";

grant insert on table "public"."companies" to "service_role";

grant references on table "public"."companies" to "service_role";

grant select on table "public"."companies" to "service_role";

grant trigger on table "public"."companies" to "service_role";

grant truncate on table "public"."companies" to "service_role";

grant update on table "public"."companies" to "service_role";

grant delete on table "public"."product_category" to "anon";

grant insert on table "public"."product_category" to "anon";

grant references on table "public"."product_category" to "anon";

grant select on table "public"."product_category" to "anon";

grant trigger on table "public"."product_category" to "anon";

grant truncate on table "public"."product_category" to "anon";

grant update on table "public"."product_category" to "anon";

grant delete on table "public"."product_category" to "authenticated";

grant insert on table "public"."product_category" to "authenticated";

grant references on table "public"."product_category" to "authenticated";

grant select on table "public"."product_category" to "authenticated";

grant trigger on table "public"."product_category" to "authenticated";

grant truncate on table "public"."product_category" to "authenticated";

grant update on table "public"."product_category" to "authenticated";

grant delete on table "public"."product_category" to "postgres";

grant insert on table "public"."product_category" to "postgres";

grant references on table "public"."product_category" to "postgres";

grant select on table "public"."product_category" to "postgres";

grant trigger on table "public"."product_category" to "postgres";

grant truncate on table "public"."product_category" to "postgres";

grant update on table "public"."product_category" to "postgres";

grant delete on table "public"."product_category" to "service_role";

grant insert on table "public"."product_category" to "service_role";

grant references on table "public"."product_category" to "service_role";

grant select on table "public"."product_category" to "service_role";

grant trigger on table "public"."product_category" to "service_role";

grant truncate on table "public"."product_category" to "service_role";

grant update on table "public"."product_category" to "service_role";

grant delete on table "public"."products" to "anon";

grant insert on table "public"."products" to "anon";

grant references on table "public"."products" to "anon";

grant select on table "public"."products" to "anon";

grant trigger on table "public"."products" to "anon";

grant truncate on table "public"."products" to "anon";

grant update on table "public"."products" to "anon";

grant delete on table "public"."products" to "authenticated";

grant insert on table "public"."products" to "authenticated";

grant references on table "public"."products" to "authenticated";

grant select on table "public"."products" to "authenticated";

grant trigger on table "public"."products" to "authenticated";

grant truncate on table "public"."products" to "authenticated";

grant update on table "public"."products" to "authenticated";

grant delete on table "public"."products" to "postgres";

grant insert on table "public"."products" to "postgres";

grant references on table "public"."products" to "postgres";

grant select on table "public"."products" to "postgres";

grant trigger on table "public"."products" to "postgres";

grant truncate on table "public"."products" to "postgres";

grant update on table "public"."products" to "postgres";

grant delete on table "public"."products" to "service_role";

grant insert on table "public"."products" to "service_role";

grant references on table "public"."products" to "service_role";

grant select on table "public"."products" to "service_role";

grant trigger on table "public"."products" to "service_role";

grant truncate on table "public"."products" to "service_role";

grant update on table "public"."products" to "service_role";

grant delete on table "public"."users" to "anon";

grant insert on table "public"."users" to "anon";

grant references on table "public"."users" to "anon";

grant select on table "public"."users" to "anon";

grant trigger on table "public"."users" to "anon";

grant truncate on table "public"."users" to "anon";

grant update on table "public"."users" to "anon";

grant delete on table "public"."users" to "authenticated";

grant insert on table "public"."users" to "authenticated";

grant references on table "public"."users" to "authenticated";

grant select on table "public"."users" to "authenticated";

grant trigger on table "public"."users" to "authenticated";

grant truncate on table "public"."users" to "authenticated";

grant update on table "public"."users" to "authenticated";

grant delete on table "public"."users" to "postgres";

grant insert on table "public"."users" to "postgres";

grant references on table "public"."users" to "postgres";

grant select on table "public"."users" to "postgres";

grant trigger on table "public"."users" to "postgres";

grant truncate on table "public"."users" to "postgres";

grant update on table "public"."users" to "postgres";

grant delete on table "public"."users" to "service_role";

grant insert on table "public"."users" to "service_role";

grant references on table "public"."users" to "service_role";

grant select on table "public"."users" to "service_role";

grant trigger on table "public"."users" to "service_role";

grant truncate on table "public"."users" to "service_role";

grant update on table "public"."users" to "service_role";

grant delete on table "public"."vendingMachine" to "anon";

grant insert on table "public"."vendingMachine" to "anon";

grant references on table "public"."vendingMachine" to "anon";

grant select on table "public"."vendingMachine" to "anon";

grant trigger on table "public"."vendingMachine" to "anon";

grant truncate on table "public"."vendingMachine" to "anon";

grant update on table "public"."vendingMachine" to "anon";

grant delete on table "public"."vendingMachine" to "authenticated";

grant insert on table "public"."vendingMachine" to "authenticated";

grant references on table "public"."vendingMachine" to "authenticated";

grant select on table "public"."vendingMachine" to "authenticated";

grant trigger on table "public"."vendingMachine" to "authenticated";

grant truncate on table "public"."vendingMachine" to "authenticated";

grant update on table "public"."vendingMachine" to "authenticated";

grant delete on table "public"."vendingMachine" to "postgres";

grant insert on table "public"."vendingMachine" to "postgres";

grant references on table "public"."vendingMachine" to "postgres";

grant select on table "public"."vendingMachine" to "postgres";

grant trigger on table "public"."vendingMachine" to "postgres";

grant truncate on table "public"."vendingMachine" to "postgres";

grant update on table "public"."vendingMachine" to "postgres";

grant delete on table "public"."vendingMachine" to "service_role";

grant insert on table "public"."vendingMachine" to "service_role";

grant references on table "public"."vendingMachine" to "service_role";

grant select on table "public"."vendingMachine" to "service_role";

grant trigger on table "public"."vendingMachine" to "service_role";

grant truncate on table "public"."vendingMachine" to "service_role";

grant update on table "public"."vendingMachine" to "service_role";


