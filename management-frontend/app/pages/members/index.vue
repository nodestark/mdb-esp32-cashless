<script setup lang="ts">
definePageMeta({ middleware: 'auth' })

import AppSidebar from '@/components/AppSidebar.vue'
import SiteHeader from '@/components/SiteHeader.vue'
import { SidebarInset, SidebarProvider } from '@/components/ui/sidebar'

const supabase = useSupabaseClient()
const { role } = useOrganization()

const members = ref<any[]>([])
const invitations = ref<any[]>([])
const loading = ref(true)

// Invite modal state
const showInviteModal = ref(false)
const inviteEmail = ref('')
const inviteRole = ref<'admin' | 'viewer'>('viewer')
const inviteLoading = ref(false)
const inviteError = ref('')
const inviteSuccess = ref('')

const isAdmin = computed(() => role.value === 'admin')

async function loadData() {
  loading.value = true
  const [membersRes, invitesRes] = await Promise.all([
    supabase.from('organization_members').select('id, created_at, user_id, role, invited_by'),
    supabase.from('invitations').select('id, created_at, email, role, token, expires_at, accepted_at, invited_by'),
  ])
  members.value = membersRes.data ?? []
  invitations.value = (invitesRes.data ?? []).filter((i: any) => !i.accepted_at)
  loading.value = false
}

onMounted(async () => {
  await loadData()
})

async function sendInvite() {
  inviteLoading.value = true
  inviteError.value = ''
  inviteSuccess.value = ''
  try {
    const { data, error } = await supabase.functions.invoke('invite-member', {
      body: { email: inviteEmail.value, role: inviteRole.value },
    })
    if (error) throw error
    if (data?.error) throw new Error(data.error)
    inviteSuccess.value = `Invitation sent. Share this token: ${data.token}`
    inviteEmail.value = ''
    inviteRole.value = 'viewer'
    await loadData()
  } catch (err: unknown) {
    inviteError.value = err instanceof Error ? err.message : 'Failed to send invitation'
  } finally {
    inviteLoading.value = false
  }
}

async function changeRole(memberId: string, newRole: string) {
  await supabase.from('organization_members').update({ role: newRole }).eq('id', memberId)
  await loadData()
}

async function removeMember(memberId: string) {
  await supabase.from('organization_members').delete().eq('id', memberId)
  await loadData()
}

async function revokeInvitation(invitationId: string) {
  await supabase.from('invitations').delete().eq('id', invitationId)
  await loadData()
}

function formatDate(dt: string) {
  return new Date(dt).toLocaleDateString()
}
</script>

<template>
  <SidebarProvider
    :style="{
      '--sidebar-width': 'calc(var(--spacing) * 72)',
      '--header-height': 'calc(var(--spacing) * 12)',
    }"
  >
    <AppSidebar variant="inset" />
    <SidebarInset>
      <SiteHeader />
      <div class="flex flex-1 flex-col gap-6 p-4 md:p-6">
        <div class="flex items-center justify-between">
          <h1 class="text-2xl font-semibold">Members</h1>
          <button
            v-if="isAdmin"
            class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90"
            @click="showInviteModal = true"
          >
            Invite member
          </button>
        </div>

        <div v-if="loading" class="text-muted-foreground">Loading members…</div>

        <template v-else>
          <!-- Active members -->
          <div>
            <h2 class="mb-3 text-base font-medium">Active members</h2>
            <div class="rounded-md border">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b bg-muted/50 text-left">
                    <th class="px-4 py-3 font-medium">User ID</th>
                    <th class="px-4 py-3 font-medium">Role</th>
                    <th class="px-4 py-3 font-medium">Joined</th>
                    <th v-if="isAdmin" class="px-4 py-3 font-medium">Actions</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="member in members"
                    :key="member.id"
                    class="border-b last:border-0 hover:bg-muted/30 transition-colors"
                  >
                    <td class="px-4 py-3 font-mono text-xs text-muted-foreground">{{ member.user_id }}</td>
                    <td class="px-4 py-3">
                      <span
                        class="rounded-full px-2 py-0.5 text-xs font-medium"
                        :class="member.role === 'admin' ? 'bg-primary/10 text-primary' : 'bg-muted text-muted-foreground'"
                      >
                        {{ member.role }}
                      </span>
                    </td>
                    <td class="px-4 py-3 text-muted-foreground">{{ formatDate(member.created_at) }}</td>
                    <td v-if="isAdmin" class="px-4 py-3">
                      <div class="flex items-center gap-2">
                        <select
                          :value="member.role"
                          class="rounded border border-input bg-background px-2 py-1 text-xs"
                          @change="changeRole(member.id, ($event.target as HTMLSelectElement).value)"
                        >
                          <option value="admin">admin</option>
                          <option value="viewer">viewer</option>
                        </select>
                        <button
                          class="text-xs text-destructive hover:underline"
                          @click="removeMember(member.id)"
                        >
                          Remove
                        </button>
                      </div>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>

          <!-- Pending invitations -->
          <div v-if="isAdmin">
            <h2 class="mb-3 text-base font-medium">Pending invitations</h2>
            <div v-if="invitations.length === 0" class="text-sm text-muted-foreground">No pending invitations.</div>
            <div v-else class="rounded-md border">
              <table class="w-full text-sm">
                <thead>
                  <tr class="border-b bg-muted/50 text-left">
                    <th class="px-4 py-3 font-medium">Email</th>
                    <th class="px-4 py-3 font-medium">Role</th>
                    <th class="px-4 py-3 font-medium">Expires</th>
                    <th class="px-4 py-3 font-medium">Actions</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="invitation in invitations"
                    :key="invitation.id"
                    class="border-b last:border-0 hover:bg-muted/30 transition-colors"
                  >
                    <td class="px-4 py-3">{{ invitation.email }}</td>
                    <td class="px-4 py-3">
                      <span class="rounded-full bg-muted px-2 py-0.5 text-xs font-medium text-muted-foreground">
                        {{ invitation.role }}
                      </span>
                    </td>
                    <td class="px-4 py-3 text-muted-foreground">{{ formatDate(invitation.expires_at) }}</td>
                    <td class="px-4 py-3">
                      <button
                        class="text-xs text-destructive hover:underline"
                        @click="revokeInvitation(invitation.id)"
                      >
                        Revoke
                      </button>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </template>
      </div>

      <!-- Invite modal -->
      <div
        v-if="showInviteModal"
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
        @click.self="showInviteModal = false"
      >
        <div class="w-full max-w-sm rounded-xl border bg-card p-6 shadow-lg">
          <h2 class="mb-4 text-lg font-semibold">Invite a member</h2>
          <form class="space-y-4" @submit.prevent="sendInvite">
            <div class="space-y-1">
              <label class="text-sm font-medium" for="invite-email">Email</label>
              <input
                id="invite-email"
                v-model="inviteEmail"
                type="email"
                required
                placeholder="colleague@example.com"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              />
            </div>
            <div class="space-y-1">
              <label class="text-sm font-medium" for="invite-role">Role</label>
              <select
                id="invite-role"
                v-model="inviteRole"
                class="flex h-9 w-full rounded-md border border-input bg-background px-3 py-1 text-sm shadow-sm focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
              >
                <option value="viewer">Viewer</option>
                <option value="admin">Admin</option>
              </select>
            </div>
            <p v-if="inviteError" class="text-sm text-destructive">{{ inviteError }}</p>
            <p v-if="inviteSuccess" class="text-sm text-green-600 break-all">{{ inviteSuccess }}</p>
            <div class="flex gap-2">
              <button
                type="button"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md border px-4 text-sm font-medium shadow-sm transition-colors hover:bg-muted"
                @click="showInviteModal = false"
              >
                Cancel
              </button>
              <button
                type="submit"
                :disabled="inviteLoading"
                class="inline-flex h-9 flex-1 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground shadow transition-colors hover:bg-primary/90 disabled:opacity-50"
              >
                <span v-if="inviteLoading">Sending…</span>
                <span v-else>Send invite</span>
              </button>
            </div>
          </form>
        </div>
      </div>
    </SidebarInset>
  </SidebarProvider>
</template>
