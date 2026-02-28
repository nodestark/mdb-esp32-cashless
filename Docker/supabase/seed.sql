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

-- 5. Create a sample embedded device
INSERT INTO public.embeddeds (id, company, owner_id, mac_address, passkey, status)
VALUES (
  'c1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'b1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'a1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'AA:BB:CC:DD:EE:FF',
  'seedpasskey1234567',
  'offline'
);

-- 6. Create a sample vending machine linked to the device
INSERT INTO public."vendingMachine" (company, embedded, name)
VALUES (
  'b1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'c1b2c3d4-e5f6-7890-abcd-ef1234567890',
  'Demo Vending Machine'
);

-- 7. Add a few sample sales for the last 7 days
INSERT INTO public.sales (embedded_id, item_price, item_number, channel, created_at)
VALUES
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 2.50, 1, 'cashless', now() - interval '1 day'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 1.75, 3, 'cashless', now() - interval '2 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 3.00, 5, 'cashless', now() - interval '3 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 2.00, 2, 'cashless', now() - interval '4 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 1.50, 7, 'cashless', now() - interval '5 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 2.25, 4, 'cashless', now() - interval '6 days'),
  ('c1b2c3d4-e5f6-7890-abcd-ef1234567890', 3.50, 1, 'cashless', now() - interval '7 days');
