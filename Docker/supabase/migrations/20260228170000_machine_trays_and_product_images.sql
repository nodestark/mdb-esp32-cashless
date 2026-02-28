-- Add image_path column to products
alter table public.products add column if not exists image_path text;

-- Create machine_trays table
create table public.machine_trays (
  id uuid not null default gen_random_uuid(),
  created_at timestamp with time zone not null default now(),
  machine_id uuid not null references public."vendingMachine"(id) on delete cascade,
  item_number integer not null,
  product_id uuid references public.products(id) on delete set null,
  capacity integer not null default 10,
  current_stock integer not null default 0,
  primary key (id),
  unique (machine_id, item_number)
);

alter table public.machine_trays enable row level security;

-- Grants
grant select, insert, update, delete on public.machine_trays to authenticated;
grant all on public.machine_trays to postgres;
grant all on public.machine_trays to service_role;

-- RLS: machine_trays belong to machines which belong to companies
create policy "machine_trays_select" on public.machine_trays
  for select to authenticated
  using (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = (select my_company_id())
    )
  );

create policy "machine_trays_insert" on public.machine_trays
  for insert to authenticated
  with check (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = (select my_company_id())
    )
    and (select i_am_admin())
  );

create policy "machine_trays_update" on public.machine_trays
  for update to authenticated
  using (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = (select my_company_id())
    )
    and (select i_am_admin())
  );

create policy "machine_trays_delete" on public.machine_trays
  for delete to authenticated
  using (
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = machine_trays.machine_id
        and vm.company = (select my_company_id())
    )
    and (select i_am_admin())
  );

-- Trigger to auto-decrement stock on new sale
create or replace function public.decrement_tray_stock()
returns trigger as $$
begin
  update public.machine_trays mt
  set current_stock = greatest(current_stock - 1, 0)
  from public."vendingMachine" vm
  where vm.embedded = NEW.embedded_id
    and mt.machine_id = vm.id
    and mt.item_number = NEW.item_number
    and mt.current_stock > 0;
  return NEW;
end;
$$ language plpgsql security definer;

create trigger decrement_tray_stock_on_sale
  after insert on public.sales
  for each row
  execute function public.decrement_tray_stock();

-- Enable realtime for machine_trays
alter publication supabase_realtime add table public.machine_trays;
