-- =========================================================
-- Add firmware_build_date column to embeddeds
-- Parsed from MQTT status payload "online|v:x.y.z|b:Mar  1 2026 14:30:00"
-- =========================================================

alter table public.embeddeds
  add column if not exists firmware_build_date timestamptz;

comment on column public.embeddeds.firmware_build_date
  is 'Firmware compilation timestamp reported by device via MQTT status message';
