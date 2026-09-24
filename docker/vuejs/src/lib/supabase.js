import { createClient } from '@supabase/supabase-js'

const supabaseUrl = 'https://supabase.vmflow.xyz'
const supabaseAnonKey = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4'

// Captured before createClient: the auth client strips the tokens from the
// URL hash asynchronously once it has turned them into a session.
const initialHash = typeof window !== 'undefined' ? window.location.hash : ''
export const isRecoveryRedirect = initialHash.includes('type=recovery')
export const authRedirectError = new URLSearchParams(initialHash.slice(1)).get('error_description')

export const supabase = createClient(
  supabaseUrl,
  supabaseAnonKey
)