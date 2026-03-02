-- =========================================================
-- Multi-tenancy migration
-- =========================================================

-- B. New tables (must come before the helper functions that reference them)

-- organization_members
create table if not exists public.organization_members (
  id          uuid not null default gen_random_uuid(),
  created_at  timestamp with time zone not null default now(),
  company_id  uuid not null references public.companies(id) on delete cascade,
  user_id     uuid not null references auth.users(id) on delete cascade,
  role        text not null check (role in ('admin', 'viewer')),
  invited_by  uuid references auth.users(id),
  constraint organization_members_pkey primary key (id),
  constraint organization_members_company_user_unique unique (company_id, user_id)
);

alter table public.organization_members enable row level security;

grant select, insert, update, delete on public.organization_members to authenticated;
grant select, insert, update, delete on public.organization_members to service_role;

-- invitations
create table if not exists public.invitations (
  id          uuid not null default gen_random_uuid(),
  created_at  timestamp with time zone not null default now(),
  company_id  uuid not null references public.companies(id) on delete cascade,
  email       text not null,
  role        text not null check (role in ('admin', 'viewer')),
  token       uuid not null default gen_random_uuid(),
  expires_at  timestamp with time zone not null default (now() + interval '7 days'),
  invited_by  uuid references auth.users(id),
  accepted_at timestamp with time zone,
  constraint invitations_pkey primary key (id),
  constraint invitations_company_email_unique unique (company_id, email)
);

alter table public.invitations enable row level security;

grant select, insert, update, delete on public.invitations to authenticated;
grant select, insert, update, delete on public.invitations to service_role;

-- paxcounter
create table if not exists public.paxcounter (
  id          uuid not null default gen_random_uuid(),
  created_at  timestamp with time zone not null default now(),
  owner_id    uuid references auth.users(id),
  embedded_id uuid not null references public.embeddeds(id) on delete cascade,
  count       integer not null default 0,
  constraint paxcounter_pkey primary key (id)
);

alter table public.paxcounter enable row level security;

grant select, insert, update, delete on public.paxcounter to service_role;
grant select on public.paxcounter to authenticated;

-- A. Helper functions (STABLE, not SECURITY DEFINER)
-- Defined after organization_members table so PostgreSQL can resolve the reference.
create or replace function public.my_company_id() returns uuid language sql stable as $$
  select company_id from public.organization_members where user_id = auth.uid() limit 1
$$;

create or replace function public.i_am_admin() returns boolean language sql stable as $$
  select exists (
    select 1 from public.organization_members
    where user_id = auth.uid() and role = 'admin'
  )
$$;

-- Fix users.company default: was gen_random_uuid() which violates the FK on insert
-- (the trigger only provides id+created_at; company must be NULL until org is assigned)
alter table public.users alter column company set default null;

-- C. Auth trigger — insert into public.users when a new auth.users row is created
create or replace function public.handle_new_user()
  returns trigger
  language plpgsql
  security definer
  set search_path = public
as $$
begin
  insert into public.users (id, created_at)
  values (new.id, new.created_at)
  on conflict (id) do nothing;
  return new;
end
$$;

drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created
  after insert on auth.users
  for each row execute procedure public.handle_new_user();

-- D. Lock down legacy tables

-- embeddeds
alter table public.embeddeds enable row level security;

revoke delete, insert, update, truncate on public.embeddeds from anon;
revoke delete, insert, update, truncate on public.embeddeds from authenticated;
revoke select on public.embeddeds from anon;

grant select on public.embeddeds to authenticated;

-- sales
alter table public.sales enable row level security;

revoke delete, insert, update, truncate on public.sales from anon;
revoke delete, insert, update, truncate on public.sales from authenticated;
revoke select on public.sales from anon;

grant select on public.sales to authenticated;

-- companies
revoke delete, insert, update, truncate on public.companies from anon;
revoke delete, insert, update, truncate on public.companies from authenticated;
revoke select on public.companies from anon;

grant select, insert on public.companies to authenticated;

-- vendingMachine
revoke delete, insert, update, truncate on public."vendingMachine" from anon;
revoke delete, insert, update, truncate on public."vendingMachine" from authenticated;
revoke select on public."vendingMachine" from anon;

grant select, insert, update, delete on public."vendingMachine" to authenticated;

-- product_category
revoke delete, insert, update, truncate on public.product_category from anon;
revoke delete, insert, update, truncate on public.product_category from authenticated;
revoke select on public.product_category from anon;

grant select, insert, update, delete on public.product_category to authenticated;

-- products
revoke delete, insert, update, truncate on public.products from anon;
revoke delete, insert, update, truncate on public.products from authenticated;
revoke select on public.products from anon;

grant select, insert, update, delete on public.products to authenticated;

-- users
revoke delete, insert, update, truncate on public.users from anon;
revoke delete, insert, update, truncate on public.users from authenticated;
revoke select on public.users from anon;

grant select, update on public.users to authenticated;

-- E. Fix existing schema bug: products.id should NOT be FK to product_category.id
--    The correct FK is products.category -> product_category.id
alter table public.products drop constraint if exists products_id_fkey;

alter table public.products
  add constraint products_category_fkey
  foreign key (category) references public.product_category(id)
  not valid;

alter table public.products validate constraint products_category_fkey;

-- F. RLS Policies

-- companies
create policy "companies_select" on public.companies
  for select to authenticated
  using (id = public.my_company_id());

-- organization_members
create policy "org_members_select" on public.organization_members
  for select to authenticated
  using (company_id = public.my_company_id());

create policy "org_members_insert" on public.organization_members
  for insert to authenticated
  with check (company_id = public.my_company_id() and public.i_am_admin());

create policy "org_members_update" on public.organization_members
  for update to authenticated
  using (company_id = public.my_company_id() and public.i_am_admin());

create policy "org_members_delete" on public.organization_members
  for delete to authenticated
  using (company_id = public.my_company_id() and public.i_am_admin());

-- invitations
create policy "invitations_select" on public.invitations
  for select to authenticated
  using (company_id = public.my_company_id() and public.i_am_admin());

create policy "invitations_insert" on public.invitations
  for insert to authenticated
  with check (company_id = public.my_company_id() and public.i_am_admin());

-- embeddeds
create policy "embeddeds_select" on public.embeddeds
  for select to authenticated
  using (company = public.my_company_id());

create policy "embeddeds_insert" on public.embeddeds
  for insert to authenticated
  with check (company = public.my_company_id() and public.i_am_admin());

create policy "embeddeds_update" on public.embeddeds
  for update to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

create policy "embeddeds_delete" on public.embeddeds
  for delete to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

-- sales (read-only; writes via service_role only)
create policy "sales_select" on public.sales
  for select to authenticated
  using (
    exists (
      select 1 from public.embeddeds e
      where e.id = sales.embedded_id
        and e.company = public.my_company_id()
    )
  );

-- vendingMachine
create policy "vendingmachine_select" on public."vendingMachine"
  for select to authenticated
  using (company = public.my_company_id());

create policy "vendingmachine_insert" on public."vendingMachine"
  for insert to authenticated
  with check (company = public.my_company_id() and public.i_am_admin());

create policy "vendingmachine_update" on public."vendingMachine"
  for update to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

create policy "vendingmachine_delete" on public."vendingMachine"
  for delete to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

-- paxcounter (read-only for authenticated; writes via service_role)
create policy "paxcounter_select" on public.paxcounter
  for select to authenticated
  using (
    exists (
      select 1 from public.embeddeds e
      where e.id = paxcounter.embedded_id
        and e.company = public.my_company_id()
    )
  );

-- users (own row only)
create policy "users_select" on public.users
  for select to authenticated
  using (id = auth.uid());

create policy "users_update" on public.users
  for update to authenticated
  using (id = auth.uid());

-- product_category
create policy "product_category_select" on public.product_category
  for select to authenticated
  using (company = public.my_company_id());

-- products
create policy "products_select" on public.products
  for select to authenticated
  using (company = public.my_company_id());
