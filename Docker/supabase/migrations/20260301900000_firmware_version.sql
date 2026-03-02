-- =========================================================
-- Add firmware_version column to embeddeds
-- Parsed from MQTT status payload "online|v:x.y.z"
-- =========================================================

alter table public.embeddeds
  add column if not exists firmware_version text;

comment on column public.embeddeds.firmware_version
  is 'Current firmware version reported by device via MQTT status message (e.g. 1.0.0)';
