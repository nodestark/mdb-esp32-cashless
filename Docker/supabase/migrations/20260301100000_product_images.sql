-- Create public product-images storage bucket
insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values (
  'product-images',
  'product-images',
  true,
  2097152, -- 2 MiB
  array['image/png', 'image/jpeg', 'image/webp']
);

-- Storage RLS policies
-- Anyone can view product images (public bucket)
create policy "product_images_select" on storage.objects
  for select
  using (bucket_id = 'product-images');

-- Authenticated admins can upload/update images
create policy "product_images_insert" on storage.objects
  for insert to authenticated
  with check (
    bucket_id = 'product-images'
    and (select public.i_am_admin())
  );

create policy "product_images_update" on storage.objects
  for update to authenticated
  using (
    bucket_id = 'product-images'
    and (select public.i_am_admin())
  );

create policy "product_images_delete" on storage.objects
  for delete to authenticated
  using (
    bucket_id = 'product-images'
    and (select public.i_am_admin())
  );
