-- =========================================================
-- Allow additional MIME types for firmware binary uploads
-- macOS detects .bin files as application/macbinary
-- =========================================================

update storage.buckets
set allowed_mime_types = array[
  'application/octet-stream',
  'application/macbinary'
]
where id = 'firmware';
