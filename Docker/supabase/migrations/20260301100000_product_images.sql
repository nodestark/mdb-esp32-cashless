-- =========================================================
-- Product images: column + storage RLS
-- =========================================================

-- A. Create the storage bucket
insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values ('product-images', 'product-images', true, 2097152, array['image/png', 'image/jpeg', 'image/webp'])
on conflict (id) do nothing;

-- B. Add image_path column to products
alter table public.products add column if not exists image_path text;

-- B. Storage RLS policies for product-images bucket
-- Allow anyone to read (public bucket serves via public URL, but SELECT on objects still needed)
create policy "product_images_select" on storage.objects
  for select
  using (bucket_id = 'product-images');

-- Authenticated admins can upload
create policy "product_images_insert" on storage.objects
  for insert to authenticated
  with check (bucket_id = 'product-images' and public.i_am_admin());

-- Authenticated admins can overwrite
create policy "product_images_update" on storage.objects
  for update to authenticated
  using (bucket_id = 'product-images' and public.i_am_admin());

-- Authenticated admins can delete
create policy "product_images_delete" on storage.objects
  for delete to authenticated
  using (bucket_id = 'product-images' and public.i_am_admin());
