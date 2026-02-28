-- =========================================================
-- Seed data for local development
-- Runs automatically after `supabase db reset`
-- =========================================================

-- 1. Create a test user in auth.users
--    Email: test@test.com / Password: test1234
INSERT INTO auth.users (
  id,
  instance_id,
  aud,
  role,
  email,
  encrypted_password,
  email_confirmed_at,
  raw_app_meta_data,
  raw_user_meta_data,
  created_at,
  updated_at,
  confirmation_token,
  recovery_token,
  email_change,
  email_change_token_new,
  email_change_token_current,
  phone,
  phone_change,
  phone_change_token,
  reauthentication_token
) VALUES (
  'a1b2c3d4-e5f6-7890-abcd-ef1234567890',
  '00000000-0000-0000-0000-000000000000',
  'authenticated',
  'authenticated',
  'test@test.com',
  crypt('test1234', gen_salt('bf')),
  now(),
  '{"provider": "email", "providers": ["email"]}',
  '{}',
  now(),
  now(),
  '',
  '',
  '',
  '',
  '',
  '',
  '',
  '',
  ''
);

-- auth.identities row (required for email/password login)
INSERT INTO auth.identities (
  id,
  user_id,
  provider_id,
  identity_data,
  provider,
  last_sign_in_at,
  created_at,
  updated_at
) VALUES (
  gen_random_uuid(),
  'a1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'test@test.com',
  jsonb_build_object('sub', 'a1b2c3d4-e5f6-7890-abcd-ef1234567890', 'email', 'test@test.com'),
  'email',
  now(),
  now(),
  now()
);

-- 2. The on_auth_user_created trigger auto-inserts into public.users,
--    but seed runs after migrations so the trigger fires above.

-- 3. Create the company
INSERT INTO public.companies (id, name)
VALUES ('b1b2c3d4-e5f6-7890-abcd-ef1234567890', 'VMflow Demo');

-- 4. Make the test user an admin of the company
INSERT INTO public.organization_members (company_id, user_id, role)
VALUES (
  'b1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'a1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'admin'
);

-- 5. Create embedded devices
INSERT INTO public.embeddeds (id, company, owner_id, mac_address, passkey, status, status_at)
VALUES
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', 'a1b2c3d4-e5f6-7890-abcd-ef1234567890', 'AA:BB:CC:DD:EE:FF', 'seedpasskey1234567', 'offline', now()),
  ('1141b1b1-2d1c-41ce-a350-c29406470db9', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', 'a1b2c3d4-e5f6-7890-abcd-ef1234567890', '30:30:f9:16:86:fc', 'seedpasskey2345678', 'online', now());

-- Ensure subdomain sequence is up-to-date
SELECT setval('embeddeds_subdomain_seq', (SELECT MAX(subdomain) FROM embeddeds));

-- 6. Create vending machines linked to devices
INSERT INTO public."vendingMachine" (id, company, embedded, name)
VALUES
  ('87a8a78a-6963-4d94-8dfa-74ff852a41b3', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', 'c1b2c3d4-e5f6-7890-abcd-ef1234567890', 'Demo Vending Machine'),
  ('fee88d2a-2caf-4bf7-a775-0396af1a192a', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', '1141b1b1-2d1c-41ce-a350-c29406470db9', 'Luciens Test Device');

-- 7. Create product categories
INSERT INTO public.product_category (id, company, name)
VALUES
  ('42a9e356-f347-4b33-a475-f3fb4e13b57c', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', 'Süßwaren'),
  ('75a6c1c3-9daf-46c8-b12f-b0f3438127bf', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', 'Getränke');

-- 8. Create products
INSERT INTO public.products (id, company, category, name, sellprice)
VALUES
  ('99745b66-516b-4303-a00e-9fe233787cfc', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', '42a9e356-f347-4b33-a475-f3fb4e13b57c', 'Haribo Pico Bala', 3),
  ('a0d1bf71-2c77-43f8-8508-e516b14ffa99', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', '75a6c1c3-9daf-46c8-b12f-b0f3438127bf', 'Red Bull Winter Edition', 2.5),
  ('25604245-d6d5-4e22-a083-705fe8e8fa93', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', '75a6c1c3-9daf-46c8-b12f-b0f3438127bf', 'Red Bull Spring Edition', 2.5),
  ('d02207e1-62e9-4c10-a3f7-39d48f38d9d8', 'b1b2c3d4-e5f6-7890-abcd-ef1234567890', '75a6c1c3-9daf-46c8-b12f-b0f3438127bf', 'Red Bull Green Edition', 2.5);

-- 9. Create machine trays for "Luciens Test Device" (20 slots)
INSERT INTO public.machine_trays (id, machine_id, item_number, product_id, capacity, current_stock)
VALUES
  ('f698cdd1-a2f3-43b9-9fef-60bb1fe2550d', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 1, '99745b66-516b-4303-a00e-9fe233787cfc', 10, 10),
  ('908723ee-483c-4423-b29a-906306d28ca7', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 2, 'd02207e1-62e9-4c10-a3f7-39d48f38d9d8', 10, 10),
  ('4ce714b3-5707-4cea-a2f9-a52e0e4f4ab8', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 3, 'a0d1bf71-2c77-43f8-8508-e516b14ffa99', 10, 10),
  ('bf08ce2a-e4be-4e11-ae7b-d260c2167767', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 4, '25604245-d6d5-4e22-a083-705fe8e8fa93', 10, 10),
  ('754ec59e-5b2b-4926-92bd-c9fbb11500bc', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 5, NULL, 10, 0),
  ('5112fbe6-55fa-4295-aba8-329df71e9b62', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 6, NULL, 10, 0),
  ('40bb66d7-71b5-4fc3-b3d4-d98cf2f41ebe', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 7, NULL, 10, 0),
  ('b31c0b75-3496-422c-b66d-af218199a97e', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 8, NULL, 10, 0),
  ('3a6e6131-160d-42ae-b67f-054680c81bee', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 9, NULL, 10, 0),
  ('ffe2f8c7-a0e9-4268-b629-10dd8e219540', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 10, NULL, 10, 0),
  ('f94357ce-7300-44a9-af0b-a29acb0b7a5c', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 11, NULL, 10, 0),
  ('8048bba6-392a-4274-b332-2f5a7b403d76', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 12, NULL, 10, 0),
  ('f8bef7e9-d51d-48bf-8527-bd52a30088c3', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 13, NULL, 10, 0),
  ('994d7dd1-d024-4845-a976-2930f9c4816a', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 14, NULL, 10, 0),
  ('2ba241c2-b868-489c-a402-cb10a18ad190', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 15, NULL, 10, 0),
  ('a41f512e-2cab-4916-b80a-bb8688ca543d', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 16, NULL, 10, 0),
  ('4a26e290-9223-4e50-abe3-63335321f593', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 17, NULL, 10, 0),
  ('a5850657-69d6-4a60-9edc-87378095829d', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 18, NULL, 10, 0),
  ('34ebd926-fdb5-4d34-bffa-5111770f326f', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 19, NULL, 10, 0),
  ('0c3ccee5-4736-4e84-8ac0-2f83390f5a6b', 'fee88d2a-2caf-4bf7-a775-0396af1a192a', 20, NULL, 10, 0);

-- 10. Add sample sales for the last 7 days
INSERT INTO public.sales (embedded_id, item_price, item_number, channel, created_at)
VALUES
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 2.50, 1, 'cashless', now() - interval '1 day'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 1.75, 3, 'cashless', now() - interval '2 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 3.00, 5, 'cashless', now() - interval '3 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 2.00, 2, 'cashless', now() - interval '4 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 1.50, 7, 'cashless', now() - interval '5 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 2.25, 4, 'cashless', now() - interval '6 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 3.50, 1, 'cashless', now() - interval '7 days');

-- 11. Add sample paxcounter data
INSERT INTO public.paxcounter (owner_id, embedded_id, count)
VALUES ('a1b2c3d4-e5f6-7890-abcd-ef1234567890', '1141b1b1-2d1c-41ce-a350-c29406470db9', 59);

-- 12. Add a used provisioning token (for reference)
INSERT INTO public.device_provisioning (company_id, short_code, expires_at, created_by, used_at, embedded_id, name)
VALUES (
  'b1b2c3d4-e5f6-7890-abcd-ef1234567890',
  '7B2BKYHB',
  now() + interval '1 hour',
  'a1b2c3d4-e5f6-7890-abcd-ef1234567890',
  now(),
  '1141b1b1-2d1c-41ce-a350-c29406470db9',
  'Luciens Test Device'
);
