FROM denoland/deno:2.2.2
WORKDIR /app
COPY main.ts .
RUN deno cache main.ts
CMD ["deno", "run", "--allow-net", "--allow-env", "main.ts"]
