-- Change firmware_build_date back to timestamptz now that the firmware
-- includes the build machine's UTC offset in the status message.
alter table public.embeddeds
  alter column firmware_build_date type timestamptz using firmware_build_date::timestamptz;
