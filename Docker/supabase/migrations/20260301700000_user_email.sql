-- Add email column to public.users so it's queryable via RLS.
-- auth.users is not accessible from the client.

alter table public.users
  add column if not exists email text;

-- Backfill existing rows from auth.users
update public.users u
set email = a.email
from auth.users a
where u.id = a.id
  and u.email is null;

-- Update the trigger to copy email on signup
create or replace function public.handle_new_user()
  returns trigger
  language plpgsql
  security definer
  set search_path = public
as $$
begin
  insert into public.users (id, created_at, email)
  values (new.id, new.created_at, new.email)
  on conflict (id) do update set email = excluded.email;
  return new;
end
$$;
