-- =========================================================
-- Seed data — reproduces the development database state
-- Runs after all migrations during `supabase db reset`
-- =========================================================

-- Fixed UUIDs for referential integrity
-- User:       7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6  (test@test.com / password123)
-- Company:    be324c63-5b64-4d83-90e0-ae9f16703a69  (VMFlow Corp)
-- Embedded:   e8f2e46c-4a97-4fc6-a01d-593e45c97276  (subdomain 1, MAC 30:30:f9:16:86:fc)
-- Machine:    2a44d02e-49cd-4b43-b191-739c0d223278  (Luciens Test Device)


-- ─── 1. Auth user ────────────────────────────────────────────────────────────
-- password: password123 (bcrypt hash)
-- The on_auth_user_created trigger auto-inserts into public.users
INSERT INTO auth.users (
  instance_id, id, aud, role, email, encrypted_password,
  email_confirmed_at, created_at, updated_at,
  raw_app_meta_data, raw_user_meta_data,
  is_super_admin, is_sso_user, is_anonymous,
  confirmation_token, recovery_token, email_change_token_new,
  email_change_token_current, email_change, reauthentication_token,
  phone, phone_change, phone_change_token
) VALUES (
  '00000000-0000-0000-0000-000000000000',
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  'authenticated', 'authenticated',
  'test@test.com',
  crypt('password123', gen_salt('bf')),
  now(), now(), now(),
  '{"provider": "email", "providers": ["email"]}',
  '{"sub": "7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6", "email": "test@test.com", "email_verified": true, "phone_verified": false}',
  false, false, false,
  '', '', '',
  '', '', '',
  '', '', ''
);

-- Supabase also needs an identity row for email login
INSERT INTO auth.identities (
  id, user_id, provider_id, identity_data, provider, last_sign_in_at, created_at, updated_at
) VALUES (
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  jsonb_build_object('sub', '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'email', 'test@test.com', 'email_verified', true),
  'email', now(), now(), now()
);


-- ─── 2. Company ──────────────────────────────────────────────────────────────
INSERT INTO public.companies (id, name)
VALUES ('be324c63-5b64-4d83-90e0-ae9f16703a69', 'VMFlow Corp');


-- ─── 3. Link user to company ────────────────────────────────────────────────
-- Update the row created by the auth trigger
UPDATE public.users
SET company = 'be324c63-5b64-4d83-90e0-ae9f16703a69',
    first_name = 'Lucien',
    last_name = 'Kerl',
    email = 'test@test.com'
WHERE id = '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6';

INSERT INTO public.organization_members (company_id, user_id, role)
VALUES (
  'be324c63-5b64-4d83-90e0-ae9f16703a69',
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  'admin'
);


-- ─── 4. Embedded device ─────────────────────────────────────────────────────
INSERT INTO public.embeddeds (id, owner_id, mac_address, status, status_at, passkey, company)
VALUES (
  'e8f2e46c-4a97-4fc6-a01d-593e45c97276',
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  '30:30:f9:16:86:fc',
  'online',
  now(),
  'q7iGs8f>Pn9sxppb0(',
  'be324c63-5b64-4d83-90e0-ae9f16703a69'
);


-- ─── 5. Vending machine ─────────────────────────────────────────────────────
INSERT INTO public."vendingMachine" (id, name, company, embedded)
VALUES (
  '2a44d02e-49cd-4b43-b191-739c0d223278',
  'Luciens Test Device',
  'be324c63-5b64-4d83-90e0-ae9f16703a69',
  'e8f2e46c-4a97-4fc6-a01d-593e45c97276'
);


-- ─── 6. Product categories ──────────────────────────────────────────────────
INSERT INTO public.product_category (id, name, company) VALUES
  ('299ad9ac-a695-4013-831b-19ad750c0b69', 'Süßwaren',  'be324c63-5b64-4d83-90e0-ae9f16703a69'),
  ('93f442e0-c48e-439c-8dea-225e22c6c8fe', 'Getränke',  'be324c63-5b64-4d83-90e0-ae9f16703a69');


-- ─── 7. Products ─────────────────────────────────────────────────────────────
INSERT INTO public.products (id, name, sellprice, company, category, image_path) VALUES
  ('1fbab67f-4fb4-4bde-9aed-de03efddcd97', 'Haribo Pico Bala',        3.00, 'be324c63-5b64-4d83-90e0-ae9f16703a69', '299ad9ac-a695-4013-831b-19ad750c0b69', '1fbab67f-4fb4-4bde-9aed-de03efddcd97.png'),
  ('c31a3469-ab69-44b6-8479-121389623c91', 'Red Bull Winter Edition',  2.50, 'be324c63-5b64-4d83-90e0-ae9f16703a69', '93f442e0-c48e-439c-8dea-225e22c6c8fe', 'c31a3469-ab69-44b6-8479-121389623c91.jpg'),
  ('05b39393-e271-43e6-bf65-ab127d7101ba', 'Red Bull Spring Edition',  2.50, 'be324c63-5b64-4d83-90e0-ae9f16703a69', '93f442e0-c48e-439c-8dea-225e22c6c8fe', '05b39393-e271-43e6-bf65-ab127d7101ba.webp'),
  ('788787df-227f-4b77-8947-143aa3b795f7', 'Red Bull Green Edition',   2.50, 'be324c63-5b64-4d83-90e0-ae9f16703a69', '93f442e0-c48e-439c-8dea-225e22c6c8fe', '788787df-227f-4b77-8947-143aa3b795f7.webp');


-- ─── 8. Machine trays (slots 1–10) ─────────────────────────────────────────
INSERT INTO public.machine_trays (machine_id, item_number, product_id, capacity, current_stock) VALUES
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  1, '1fbab67f-4fb4-4bde-9aed-de03efddcd97', 10, 10),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  2, '05b39393-e271-43e6-bf65-ab127d7101ba', 10,  9),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  3, '788787df-227f-4b77-8947-143aa3b795f7', 10,  9),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  4, 'c31a3469-ab69-44b6-8479-121389623c91', 10, 10),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  5, NULL, 10, 0),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  6, NULL, 10, 0),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  7, NULL, 10, 0),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  8, NULL, 10, 0),
  ('2a44d02e-49cd-4b43-b191-739c0d223278',  9, NULL, 10, 0),
  ('2a44d02e-49cd-4b43-b191-739c0d223278', 10, NULL, 10, 0);


-- ─── 9. Sales (machine_id stamped by BEFORE INSERT trigger) ─────────────────
-- Note: the trigger auto-sets machine_id from vendingMachine.embedded
INSERT INTO public.sales (owner_id, embedded_id, item_price, item_number, channel, created_at) VALUES
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 2.79, 5, 'mqtt', now() - interval '9 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 4.50, 7, 'mqtt', now() - interval '8 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 3.59, 2, 'mqtt', now() - interval '7 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 4.40, 6, 'mqtt', now() - interval '6 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 4.59, 6, 'mqtt', now() - interval '5 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 2.29, 7, 'mqtt', now() - interval '4 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 1.50, 5, 'mqtt', now() - interval '3 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 4.69, 6, 'mqtt', now() - interval '2 hours'),
  ('7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6', 'e8f2e46c-4a97-4fc6-a01d-593e45c97276', 4.59, 3, 'mqtt', now() - interval '1 hour');


-- ─── 10. Paxcounter ─────────────────────────────────────────────────────────
INSERT INTO public.paxcounter (owner_id, embedded_id, count)
VALUES (
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  'e8f2e46c-4a97-4fc6-a01d-593e45c97276',
  1
);


-- ─── 11. Device provisioning (used token) ───────────────────────────────────
INSERT INTO public.device_provisioning (company_id, short_code, expires_at, created_by, used_at, embedded_id, name, device_only)
VALUES (
  'be324c63-5b64-4d83-90e0-ae9f16703a69',
  'B5M7EE2W',
  now() + interval '1 hour',
  '7ee6e3e3-bfe1-412c-9c0e-b0d95bf98ac6',
  now(),
  'e8f2e46c-4a97-4fc6-a01d-593e45c97276',
  'Luciens Test Device',
  false
);
