-- Fix: grant insert/update/delete on embeddeds to authenticated role.
-- The multitenancy migration (20260228000000) revoked all DML privileges but
-- only granted back SELECT, making the RLS insert/update/delete policies
-- ineffective (error 42501 "permission denied for table embeddeds").

grant insert, update, delete on public.embeddeds to authenticated;
