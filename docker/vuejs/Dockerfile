# VMflow dashboard: build the Vue SPA, serve it with nginx.
FROM node:22-alpine AS build
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY . .
# Vite inlines these at build time; they are public values (URL + anon key).
ARG VITE_SUPABASE_URL
ARG VITE_SUPABASE_ANON_KEY
RUN test -n "$VITE_SUPABASE_URL" && test -n "$VITE_SUPABASE_ANON_KEY" \
  || (echo "VITE_SUPABASE_URL / VITE_SUPABASE_ANON_KEY build args are required" >&2; exit 1)
RUN npm run build

FROM nginx:stable-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 5173
