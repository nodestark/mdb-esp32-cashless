-- Push subscriptions: stores Web Push API subscription objects per device per user
CREATE TABLE IF NOT EXISTS public.push_subscriptions (
    id         uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    created_at timestamptz NOT NULL DEFAULT now(),
    user_id    uuid NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    endpoint   text NOT NULL,
    p256dh     text NOT NULL,
    auth       text NOT NULL,
    UNIQUE (user_id, endpoint)
);

ALTER TABLE public.push_subscriptions ENABLE ROW LEVEL SECURITY;

GRANT SELECT, INSERT, DELETE ON public.push_subscriptions TO authenticated;
GRANT ALL ON public.push_subscriptions TO service_role;

CREATE POLICY push_subscriptions_select ON public.push_subscriptions
    FOR SELECT TO authenticated
    USING (user_id = auth.uid());

CREATE POLICY push_subscriptions_insert ON public.push_subscriptions
    FOR INSERT TO authenticated
    WITH CHECK (user_id = auth.uid());

CREATE POLICY push_subscriptions_delete ON public.push_subscriptions
    FOR DELETE TO authenticated
    USING (user_id = auth.uid());

-- Notification preferences: per-user toggle per notification type
-- Design: absence of a row = enabled by default (opt-in).
-- Only rows with enabled=false are meaningful for filtering.
CREATE TABLE IF NOT EXISTS public.notification_preferences (
    id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    created_at        timestamptz NOT NULL DEFAULT now(),
    updated_at        timestamptz NOT NULL DEFAULT now(),
    user_id           uuid NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    notification_type text NOT NULL,
    enabled           boolean NOT NULL DEFAULT true,
    UNIQUE (user_id, notification_type)
);

ALTER TABLE public.notification_preferences ENABLE ROW LEVEL SECURITY;

GRANT SELECT, INSERT, UPDATE, DELETE ON public.notification_preferences TO authenticated;
GRANT ALL ON public.notification_preferences TO service_role;

CREATE POLICY notification_preferences_select ON public.notification_preferences
    FOR SELECT TO authenticated
    USING (user_id = auth.uid());

CREATE POLICY notification_preferences_insert ON public.notification_preferences
    FOR INSERT TO authenticated
    WITH CHECK (user_id = auth.uid());

CREATE POLICY notification_preferences_update ON public.notification_preferences
    FOR UPDATE TO authenticated
    USING (user_id = auth.uid());

CREATE POLICY notification_preferences_delete ON public.notification_preferences
    FOR DELETE TO authenticated
    USING (user_id = auth.uid());
