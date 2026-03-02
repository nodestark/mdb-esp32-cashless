-- Fix infinite recursion: my_company_id() and i_am_admin() query
-- organization_members, whose RLS policies call my_company_id(),
-- causing a stack overflow.
--
-- SECURITY DEFINER makes these functions run as the defining role
-- (postgres), bypassing RLS on the tables they read.

create or replace function public.my_company_id() returns uuid
  language sql stable security definer
  set search_path = ''
as $$
  select company_id from public.organization_members where user_id = auth.uid() limit 1
$$;

create or replace function public.i_am_admin() returns boolean
  language sql stable security definer
  set search_path = ''
as $$
  select exists (
    select 1 from public.organization_members
    where user_id = auth.uid() and role = 'admin'
  )
$$;
