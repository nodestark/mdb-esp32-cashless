-- Track which migrations have been applied
create table if not exists public._migrations (
  name text primary key,
  applied_at timestamptz not null default now()
);

-- Backfill: mark all existing migrations as already applied
-- (this migration runs after all previous ones in setup.sh)
insert into public._migrations (name)
values
  ('20260103214225_initial_schema.sql'),
  ('20260109205747_extension.sql'),
  ('20260228000000_multitenancy.sql'),
  ('20260228130000_device_provisioning.sql'),
  ('20260228140000_fix_rls_recursion.sql'),
  ('20260228150000_provisioning_name.sql'),
  ('20260228160000_enable_realtime.sql'),
  ('20260228170000_machine_trays_and_product_images.sql'),
  ('20260301000000_product_trays.sql'),
  ('20260301100000_product_images.sql'),
  ('20260301200000_device_swap.sql'),
  ('20260301300000_device_only_provisioning.sql'),
  ('20260301400000_device_delete_fks.sql'),
  ('20260301500000_invitations_delete_policy.sql'),
  ('20260301600000_user_names.sql'),
  ('20260301700000_user_email.sql'),
  ('20260301800000_firmware_ota.sql'),
  ('20260301900000_firmware_version.sql'),
  ('20260302000000_firmware_build_date.sql'),
  ('20260302100000_firmware_bucket_mime.sql'),
  ('20260302200000_firmware_build_date_text.sql'),
  ('20260302300000_firmware_build_date_timestamptz.sql'),
  ('20260302400000_provisioning_delete_policy.sql'),
  ('20260302500000_migration_tracking.sql')
on conflict (name) do nothing;
