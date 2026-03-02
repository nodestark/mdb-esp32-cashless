-- Allow admins to delete (revoke) pending provisioning tokens
create policy "device_provisioning_delete" on public.device_provisioning
  for delete to authenticated
  using (company_id = public.my_company_id() and public.i_am_admin());

grant delete on public.device_provisioning to authenticated;
