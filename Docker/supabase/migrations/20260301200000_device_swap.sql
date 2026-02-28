-- =========================================================
-- Device swap support: add machine_id to sales & paxcounter
-- =========================================================

-- A. Fix vendingMachine.embedded default (was gen_random_uuid(), should be NULL)
alter table public."vendingMachine" alter column embedded set default null;

-- B. Add unique partial index — one device per machine
create unique index if not exists vendingmachine_embedded_unique
  on public."vendingMachine"(embedded)
  where embedded is not null;

-- C. Add machine_id to sales
alter table public.sales
  add column machine_id uuid references public."vendingMachine"(id) on delete set null;

-- Backfill from current vendingMachine assignments
update public.sales s
set machine_id = vm.id
from public."vendingMachine" vm
where vm.embedded = s.embedded_id
  and s.machine_id is null;

create index idx_sales_machine_id on public.sales(machine_id);

-- D. Add machine_id to paxcounter
alter table public.paxcounter
  add column machine_id uuid references public."vendingMachine"(id) on delete set null;

update public.paxcounter p
set machine_id = vm.id
from public."vendingMachine" vm
where vm.embedded = p.embedded_id
  and p.machine_id is null;

create index idx_paxcounter_machine_id on public.paxcounter(machine_id);

-- E. Replace the sales trigger: stamp machine_id + decrement stock (BEFORE INSERT)
drop trigger if exists decrement_tray_stock_on_sale on public.sales;
drop trigger if exists on_sale_decrement_stock on public.sales;

create or replace function public.stamp_machine_and_decrement_stock()
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

  -- Stamp machine_id on the sale row
  NEW.machine_id := v_machine_id;

  -- Decrement tray stock if machine found
  if v_machine_id is not null then
    update public.machine_trays
    set current_stock = greatest(0, current_stock - 1)
    where machine_id = v_machine_id
      and item_number = NEW.item_number;
  end if;

  return NEW;
end
$$;

create trigger on_sale_stamp_machine_and_decrement_stock
  before insert on public.sales
  for each row execute function public.stamp_machine_and_decrement_stock();

-- F. Paxcounter trigger: stamp machine_id (BEFORE INSERT)
create or replace function public.stamp_paxcounter_machine()
  returns trigger
  language plpgsql
  security definer
  set search_path = ''
as $$
begin
  select vm.id into NEW.machine_id
  from public."vendingMachine" vm
  where vm.embedded = NEW.embedded_id
  limit 1;

  return NEW;
end
$$;

create trigger on_paxcounter_stamp_machine
  before insert on public.paxcounter
  for each row execute function public.stamp_paxcounter_machine();

-- G. Update RLS policies to also allow access via machine_id
drop policy if exists "sales_select" on public.sales;

create policy "sales_select" on public.sales
  for select to authenticated
  using (
    exists (
      select 1 from public.embeddeds e
      where e.id = sales.embedded_id
        and e.company = public.my_company_id()
    )
    or
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = sales.machine_id
        and vm.company = public.my_company_id()
    )
  );

drop policy if exists "paxcounter_select" on public.paxcounter;

create policy "paxcounter_select" on public.paxcounter
  for select to authenticated
  using (
    exists (
      select 1 from public.embeddeds e
      where e.id = paxcounter.embedded_id
        and e.company = public.my_company_id()
    )
    or
    exists (
      select 1 from public."vendingMachine" vm
      where vm.id = paxcounter.machine_id
        and vm.company = public.my_company_id()
    )
  );
