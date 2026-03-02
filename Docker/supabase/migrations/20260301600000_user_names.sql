-- Add first_name / last_name to public.users and allow same-company members
-- to see each other's names in the members list.

alter table public.users
  add column if not exists first_name text,
  add column if not exists last_name  text;

-- Allow members of the same company to read each other's user rows.
-- The existing "users_select" policy only allows id = auth.uid() (own row).
create policy "users_select_same_company" on public.users
  for select to authenticated
  using (company = public.my_company_id());
