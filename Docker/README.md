# VMflow.xyz Docker

This is a minimal Docker Compose setup for self-hosting VMflow.xyz

## Setup

1. Copy the Docker-Compose file `Docker/docker-compose.yml` to your desired destination folder.
2. Copy the `Docker/.env.example` file and rename it to `.env`
3. Change the secrets yourself or run the following script to automatically generate them:
    - `bash <(curl -s https://raw.githubusercontent.com/supabase/supabase/refs/heads/master/docker/utils/generate-keys.sh)`

4. Change the following Parameters to your desire:
    - `SUPABASE_PUBLIC_URL`: the public facing URL (e.g. https://supabase.vmflow.xyz)
    - `API_EXTERNAL_URL`: the public facing API URL (e.g. https://supabase.vmflow.xyz)
    most of the time these two values can be the same value unless you want something special
    - `N8N_HOST`: the hostname for the n8n host (e.g. n8n.vmflow.xyz)