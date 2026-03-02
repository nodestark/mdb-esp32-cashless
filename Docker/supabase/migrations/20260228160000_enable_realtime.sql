-- Enable realtime on embeddeds, vendingMachine, and sales tables.
-- REPLICA IDENTITY FULL is required so Supabase Realtime can deliver
-- the full new row in UPDATE/INSERT payloads and evaluate RLS policies.
alter table "public"."embeddeds" replica identity full;
alter table "public"."vendingMachine" replica identity full;
alter table "public"."sales" replica identity full;

alter publication supabase_realtime add table "public"."embeddeds";
alter publication supabase_realtime add table "public"."vendingMachine";
alter publication supabase_realtime add table "public"."sales";
