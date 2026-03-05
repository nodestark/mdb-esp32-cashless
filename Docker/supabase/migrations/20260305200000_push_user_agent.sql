-- Add user_agent to push_subscriptions so users can identify their registered devices
ALTER TABLE public.push_subscriptions
  ADD COLUMN IF NOT EXISTS user_agent text;
