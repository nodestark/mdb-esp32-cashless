-- RLS policies for machine_coils
-- Ownership is resolved through the machines table

CREATE POLICY "select_policy" ON "public"."machine_coils"
  FOR SELECT TO "authenticated"
  USING (
    EXISTS (
      SELECT 1 FROM "public"."machines" m
      WHERE m.id = machine_coils.machine_id
        AND m.owner_id = "auth"."uid"()
    )
  );

CREATE POLICY "update_policy" ON "public"."machine_coils"
  FOR UPDATE TO "authenticated"
  USING (
    EXISTS (
      SELECT 1 FROM "public"."machines" m
      WHERE m.id = machine_coils.machine_id
        AND m.owner_id = "auth"."uid"()
    )
  )
  WITH CHECK (
    EXISTS (
      SELECT 1 FROM "public"."machines" m
      WHERE m.id = machine_coils.machine_id
        AND m.owner_id = "auth"."uid"()
    )
  );
