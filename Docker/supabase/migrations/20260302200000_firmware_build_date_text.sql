-- Change firmware_build_date from timestamptz to text
-- The build date comes from the C compiler's __DATE__ __TIME__ macros (naive local time, no timezone)
-- Storing as text avoids timezone conversion issues
alter table public.embeddeds
  alter column firmware_build_date type text using firmware_build_date::text;
