<script setup lang="ts">
import { IconTrendingUp } from "@tabler/icons-vue"

import { Badge } from '@/components/ui/badge'
import {
  Card,
  CardAction,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from '@/components/ui/card'

const props = defineProps<{
  todaySales: number
  weekSales: number
  machinesOnline: number
  totalMachines: number
}>()

function formatCurrency(amount: number) {
  return new Intl.NumberFormat('en-US', { style: 'currency', currency: 'EUR' }).format(amount)
}
</script>

<template>
  <div class="*:data-[slot=card]:from-primary/5 *:data-[slot=card]:to-card dark:*:data-[slot=card]:bg-card grid grid-cols-1 gap-4 px-4 *:data-[slot=card]:bg-gradient-to-t *:data-[slot=card]:shadow-xs lg:px-6 @xl/main:grid-cols-2 @5xl/main:grid-cols-4">
    <Card class="@container/card">
      <CardHeader>
        <CardDescription>Today's Sales</CardDescription>
        <CardTitle class="text-2xl font-semibold tabular-nums @[250px]/card:text-3xl">
          {{ formatCurrency(todaySales) }}
        </CardTitle>
        <CardAction>
          <Badge variant="outline">
            <IconTrendingUp />
            Today
          </Badge>
        </CardAction>
      </CardHeader>
      <CardFooter class="flex-col items-start gap-1.5 text-sm">
        <div class="text-muted-foreground">Total revenue today</div>
      </CardFooter>
    </Card>

    <Card class="@container/card">
      <CardHeader>
        <CardDescription>This Week's Sales</CardDescription>
        <CardTitle class="text-2xl font-semibold tabular-nums @[250px]/card:text-3xl">
          {{ formatCurrency(weekSales) }}
        </CardTitle>
        <CardAction>
          <Badge variant="outline">
            <IconTrendingUp />
            7 days
          </Badge>
        </CardAction>
      </CardHeader>
      <CardFooter class="flex-col items-start gap-1.5 text-sm">
        <div class="text-muted-foreground">Total revenue this week</div>
      </CardFooter>
    </Card>

    <Card class="@container/card">
      <CardHeader>
        <CardDescription>Machines Online</CardDescription>
        <CardTitle class="text-2xl font-semibold tabular-nums @[250px]/card:text-3xl">
          {{ machinesOnline }}
        </CardTitle>
        <CardAction>
          <Badge variant="outline">
            <IconTrendingUp />
            Live
          </Badge>
        </CardAction>
      </CardHeader>
      <CardFooter class="flex-col items-start gap-1.5 text-sm">
        <div class="text-muted-foreground">{{ machinesOnline }} of {{ totalMachines }} online</div>
      </CardFooter>
    </Card>

    <Card class="@container/card">
      <CardHeader>
        <CardDescription>Total Machines</CardDescription>
        <CardTitle class="text-2xl font-semibold tabular-nums @[250px]/card:text-3xl">
          {{ totalMachines }}
        </CardTitle>
        <CardAction>
          <Badge variant="outline">
            <IconTrendingUp />
            Fleet
          </Badge>
        </CardAction>
      </CardHeader>
      <CardFooter class="flex-col items-start gap-1.5 text-sm">
        <div class="text-muted-foreground">Registered vending machines</div>
      </CardFooter>
    </Card>
  </div>
</template>
