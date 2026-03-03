FROM python:3.10-alpine
WORKDIR /usr/local/app

# Run commands during image builder
RUN pip install --no-cache-dir supabase paho-mqtt

# Run commands during start container
CMD ["python", "-u", "/clients/mqtt_domain.py"]