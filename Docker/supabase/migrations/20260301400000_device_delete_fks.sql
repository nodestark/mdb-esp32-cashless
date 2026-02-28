-- Allow deleting an embedded device without losing sales/paxcounter history.
-- sales.machine_id and paxcounter.machine_id preserve the link to the machine.

-- sales.embedded_id → SET NULL on delete
alter table public.sales drop constraint sales_embedded_id_fkey;
alter table public.sales
  add constraint sales_embedded_id_fkey
  foreign key (embedded_id) references public.embeddeds(id) on delete set null;

-- paxcounter.embedded_id → SET NULL on delete (was CASCADE, which would destroy history)
alter table public.paxcounter drop constraint paxcounter_embedded_id_fkey;
alter table public.paxcounter
  add constraint paxcounter_embedded_id_fkey
  foreign key (embedded_id) references public.embeddeds(id) on delete set null;

-- device_provisioning.embedded_id → SET NULL on delete
alter table public.device_provisioning drop constraint device_provisioning_embedded_id_fkey;
alter table public.device_provisioning
  add constraint device_provisioning_embedded_id_fkey
  foreign key (embedded_id) references public.embeddeds(id) on delete set null;

-- vendingMachine.embedded → SET NULL on delete (unassigns machine automatically)
alter table public."vendingMachine" drop constraint "vendingMachine_embedded_fkey";
alter table public."vendingMachine"
  add constraint "vendingMachine_embedded_fkey"
  foreign key (embedded) references public.embeddeds(id) on delete set null;
