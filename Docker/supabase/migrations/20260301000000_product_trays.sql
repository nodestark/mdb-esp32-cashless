-- =========================================================
-- Product trays & stock tracking migration
-- =========================================================

-- A. machine_trays table
create table if not exists public.machine_trays (
  id            uuid not null default gen_random_uuid(),
  created_at    timestamp with time zone not null default now(),
  machine_id    uuid not null references public."vendingMachine"(id) on delete cascade,
  item_number   bigint not null,
  product_id    uuid references public.products(id) on delete set null,
  capacity      integer not null default 10,
  current_stock integer not null default 0,
  constraint machine_trays_pkey primary key (id),
  constraint machine_trays_machine_item_unique unique (machine_id, item_number),
  constraint machine_trays_capacity_positive check (capacity > 0),
  constraint machine_trays_stock_bounds check (current_stock >= 0 and current_stock <= capacity)
);

alter table public.machine_trays enable row level security;

grant select, insert, update, delete on public.machine_trays to authenticated;
grant select, insert, update, delete on public.machine_trays to service_role;

-- B. RLS policies for machine_trays (via join to vendingMachine.company)
drop policy if exists "machine_trays_select" on public.machine_trays;
drop policy if exists "machine_trays_insert" on public.machine_trays;
drop policy if exists "machine_trays_update" on public.machine_trays;
drop policy if exists "machine_trays_delete" on public.machine_trays;

create policy "machine_trays_select" on public.machine_trays
  for select to authenticated
  using (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = public.my_company_id()
    )
  );

create policy "machine_trays_insert" on public.machine_trays
  for insert to authenticated
  with check (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = public.my_company_id()
    )
    and public.i_am_admin()
  );

create policy "machine_trays_update" on public.machine_trays
  for update to authenticated
  using (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = public.my_company_id()
    )
    and public.i_am_admin()
  );

create policy "machine_trays_delete" on public.machine_trays
  for delete to authenticated
  using (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = public.my_company_id()
    )
    and public.i_am_admin()
  );

-- C. Stock decrement trigger on sales insert
create or replace function public.decrement_tray_stock()
  returns trigger
  language plpgsql
  security definer
  set search_path = ''
as $$
declare
  v_machine_id uuid;
begin
  -- Resolve machine_id from the embedded device
  select vm.id into v_machine_id
  from public."vendingMachine" vm
  where vm.embedded = NEW.embedded_id
  limit 1;

  if v_machine_id is null then
    return NEW;
  end if;

  update public.machine_trays
  set current_stock = greatest(0, current_stock - 1)
  where machine_id = v_machine_id
    and item_number = NEW.item_number;

  return NEW;
end
$$;

create trigger on_sale_decrement_stock
  after insert on public.sales
  for each row execute procedure public.decrement_tray_stock();

-- D. Missing CRUD policies for products and product_category
create policy "product_category_insert" on public.product_category
  for insert to authenticated
  with check (company = public.my_company_id() and public.i_am_admin());

create policy "product_category_update" on public.product_category
  for update to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

create policy "product_category_delete" on public.product_category
  for delete to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

create policy "products_insert" on public.products
  for insert to authenticated
  with check (company = public.my_company_id() and public.i_am_admin());

create policy "products_update" on public.products
  for update to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

create policy "products_delete" on public.products
  for delete to authenticated
  using (company = public.my_company_id() and public.i_am_admin());

-- E. Realtime for machine_trays (skip if already added by earlier migration)
do $$
begin
  if not exists (
    select 1 from pg_publication_tables
    where pubname = 'supabase_realtime' and tablename = 'machine_trays'
  ) then
    alter publication supabase_realtime add table public.machine_trays;
  end if;
end $$;
