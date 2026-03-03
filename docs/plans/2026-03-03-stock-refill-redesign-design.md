# Stock-First Machine Redesign — Design Document

**Date:** 2026-03-03
**Status:** Approved

## Problem

Operators use mobile devices as their primary tool. The current `/machines` page focuses on revenue/sales stats — stock info is a placeholder ("--"). To check which machines need refilling, operators must tap into each machine's detail page and navigate to the Trays & Stock tab. This is slow and frustrating when managing many machines.

## Goal

Redesign `/machines` as a stock-first, mobile-optimized page so operators can:
1. Instantly see which machines need refilling (sorted by urgency)
2. See a per-machine packing list (what products and how many to bring)
3. Tap through to a focused tray-by-tray refill flow

## Approach

**Approach A (selected):** Replace `/machines` with a stock-urgency dashboard. Revenue stats remain on the `/` dashboard page.

Alternatives considered:
- **B: Two-mode cards** — Revenue/Stock toggle on `/machines`. Adds complexity, clutters mobile.
- **C: Dedicated /refill page** — Separate workflow page. More code to maintain, doesn't match operator mental model of "my machines."

## Design

### Data Layer

Extend `useMachines` composable to fetch stock stats alongside existing machine data.

**New fields per machine:**

| Field | Type | Description |
|-------|------|-------------|
| `total_trays` | `number` | Total configured trays |
| `low_trays` | `number` | Trays where `current_stock <= min_stock` |
| `empty_trays` | `number` | Trays where `current_stock === 0` |
| `stock_health` | `'ok' \| 'low' \| 'critical'` | Derived: critical if any empty, low if any below min, ok otherwise |
| `tray_summary` | `{ product_name, product_id, deficit }[]` | Per-product units needed to refill low trays to capacity |

**Query approach:** Single batch query fetching `machine_trays` joined with `products` for all company machines, aggregated per machine in JS. Avoids N+1 queries.

**Sort order:** Machines sorted by urgency — critical first (has empties), then low, then ok. Within each tier, sort by number of low trays descending.

### `/machines` Page Layout (Mobile-First)

**Header:** Page title "Machines" + "Add Machine" button (admin only). No aggregate packing list — operators pack per machine into separate boxes.

**Machine cards (urgency-sorted):**

Machines needing refill show an expanded card:

```
┌─────────────────────────────────┐
│  Office Lobby              🔴   │
│  2 empty · 3 low  (of 12)      │
│  ▓▓▓▓▓▓▓▓░░░░░░░░  42%        │
│                                 │
│  Pack for this machine:         │
│  8× Coffee                      │
│  6× Cola                        │
│  4× Water                       │
│                                 │
│  [Refill →]                     │
└─────────────────────────────────┘
```

- Status dot: red (critical/has empties), amber (has low trays), green (all ok)
- Stock bar: visual fill percentage across all trays
- Packing list: products and quantities needed, derived from `tray_summary`
- "Refill" button: navigates to `/machines/[id]?tab=stock`

Healthy machines show a compact card:

```
┌─────────────────────────────────┐
│  Break Room                🟢   │
│  All stocked  (10 trays)        │
└─────────────────────────────────┘
```

### Refill Flow (Machine Detail Page)

Reuses the existing `/machines/[id]` Trays & Stock tab. Changes:

1. **Tab default from query param:** When `?tab=stock` is present, default to the Trays & Stock tab instead of Sales
2. **Low tray focus:** Low/empty trays are visually prominent, healthy trays are dimmed
3. **Tray-by-tray confirmation:** Operator taps "Fill to full" per tray using existing `refillToFull()` composable function
4. **Done navigation:** Once all trays are refilled (or manually), a "Done — back to machines" link appears

No new composable logic needed — the existing `useMachineTrays` handles all refill operations and activity logging.

### Real-Time Updates

- Extend the existing `useMachines` realtime subscription to include `machine_trays` table changes
- When stock changes (sales decrement or refill), re-aggregate stock stats for the affected machine card
- Live updates mean: if another operator refills a machine, the list reflects it immediately

### What's Removed from `/machines`

The following move to the `/` dashboard page (which already has revenue KPIs):
- Today/yesterday revenue per machine
- Sales count per machine
- Last sale time
- Paxcounter traffic

These are not removed from the system — just from the machine list cards to make room for stock-focused info.

## Files Affected

| File | Change |
|------|--------|
| `management-frontend/app/pages/machines/index.vue` | Redesign card layout from revenue to stock-urgency |
| `management-frontend/app/composables/useMachines.ts` | Add stock aggregation query + machine_trays realtime subscription |
| `management-frontend/app/pages/machines/[id].vue` | Read `?tab=stock` query param for default tab; add low-tray focus styling; add "done" navigation |

## Out of Scope

- Offline support (can add later)
- Aggregate packing list across machines (operators pack per machine)
- Temperature monitoring (no sensor data yet)
- Route optimization
