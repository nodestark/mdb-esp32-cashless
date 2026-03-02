-- Allow provisioning a device without auto-creating a vendingMachine
alter table public.device_provisioning
  add column if not exists device_only boolean not null default false;
