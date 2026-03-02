-- =========================================================
-- Firmware OTA (Over-The-Air) updates
-- =========================================================

-- firmware_versions: tracks uploaded firmware binaries per company
create table if not exists public.firmware_versions (
  id            uuid not null default gen_random_uuid(),
  created_at    timestamp with time zone not null default now(),
  company_id    uuid not null references public.companies(id) on delete cascade,
  version_label text not null,
  file_path     text not null,
  file_size     bigint,
  notes         text,
  uploaded_by   uuid references auth.users(id),
  constraint firmware_versions_pkey primary key (id),
  constraint firmware_versions_company_version_unique unique (company_id, version_label)
);

alter table public.firmware_versions enable row level security;

grant select on public.firmware_versions to authenticated;
grant select, insert, update, delete on public.firmware_versions to service_role;

-- RLS: company members can read, admins can write
create policy "firmware_versions_select" on public.firmware_versions
  for select to authenticated
  using (company_id = public.my_company_id());

create policy "firmware_versions_insert" on public.firmware_versions
  for insert to authenticated
  with check (company_id = public.my_company_id() and public.i_am_admin());

create policy "firmware_versions_delete" on public.firmware_versions
  for delete to authenticated
  using (company_id = public.my_company_id() and public.i_am_admin());

-- Allow admin INSERT/DELETE grants for managing firmware
grant insert, delete on public.firmware_versions to authenticated;

-- ota_updates: log of OTA update triggers sent to devices
create table if not exists public.ota_updates (
  id                  uuid not null default gen_random_uuid(),
  created_at          timestamp with time zone not null default now(),
  embedded_id         uuid not null references public.embeddeds(id) on delete cascade,
  firmware_version_id uuid not null references public.firmware_versions(id) on delete cascade,
  triggered_by        uuid references auth.users(id),
  status              text not null default 'triggered',
  constraint ota_updates_pkey primary key (id)
);

alter table public.ota_updates enable row level security;

grant select on public.ota_updates to authenticated;
grant select, insert, update, delete on public.ota_updates to service_role;

-- RLS: visible to company members via embedded device ownership
create policy "ota_updates_select" on public.ota_updates
  for select to authenticated
  using (
    exists (
      select 1 from public.embeddeds e
      where e.id = ota_updates.embedded_id
        and e.company = public.my_company_id()
    )
  );

create policy "ota_updates_insert" on public.ota_updates
  for insert to authenticated
  with check (
    exists (
      select 1 from public.embeddeds e
      where e.id = ota_updates.embedded_id
        and e.company = public.my_company_id()
    )
    and public.i_am_admin()
  );

grant insert on public.ota_updates to authenticated;

-- Create firmware storage bucket (public read for device downloads)
insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values (
  'firmware',
  'firmware',
  true,
  5242880,  -- 5 MiB
  array['application/octet-stream']
)
on conflict (id) do nothing;

-- Storage RLS: anyone can read (devices need unauthenticated access), admins can write
create policy "firmware_public_read" on storage.objects
  for select using (bucket_id = 'firmware');

create policy "firmware_admin_insert" on storage.objects
  for insert to authenticated
  with check (bucket_id = 'firmware' and public.i_am_admin());

create policy "firmware_admin_update" on storage.objects
  for update to authenticated
  using (bucket_id = 'firmware' and public.i_am_admin());

create policy "firmware_admin_delete" on storage.objects
  for delete to authenticated
  using (bucket_id = 'firmware' and public.i_am_admin());
