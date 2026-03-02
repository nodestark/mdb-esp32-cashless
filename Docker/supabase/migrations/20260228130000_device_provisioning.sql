-- =========================================================
-- Device provisioning tokens
-- =========================================================

create table if not exists public.device_provisioning (
  id          uuid not null default gen_random_uuid(),
  created_at  timestamp with time zone not null default now(),
  company_id  uuid not null references public.companies(id) on delete cascade,
  short_code  text not null,
  expires_at  timestamp with time zone not null default (now() + interval '1 hour'),
  created_by  uuid references auth.users(id),
  used_at     timestamp with time zone,
  embedded_id uuid references public.embeddeds(id),
  constraint device_provisioning_pkey primary key (id),
  constraint device_provisioning_short_code_key unique (short_code)
);

alter table public.device_provisioning enable row level security;

-- Admins can see and create tokens for their own company
create policy "device_provisioning_select" on public.device_provisioning
  for select to authenticated
  using (company_id = public.my_company_id() and public.i_am_admin());

create policy "device_provisioning_insert" on public.device_provisioning
  for insert to authenticated
  with check (company_id = public.my_company_id() and public.i_am_admin());

grant select, insert on public.device_provisioning to authenticated;
grant select, insert, update on public.device_provisioning to service_role;
