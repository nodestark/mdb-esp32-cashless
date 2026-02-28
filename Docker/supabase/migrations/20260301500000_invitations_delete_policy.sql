-- Allow admins to delete (revoke) and update pending invitations.

create policy "invitations_delete" on public.invitations
  for delete to authenticated
  using (company_id = public.my_company_id() and public.i_am_admin());

create policy "invitations_update" on public.invitations
  for update to authenticated
  using  (company_id = public.my_company_id() and public.i_am_admin())
  with check (company_id = public.my_company_id() and public.i_am_admin());
