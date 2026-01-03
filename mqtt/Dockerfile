FROM python:3.10-alpine
WORKDIR /usr/local/app

# Install the application dependencies
RUN pip install --no-cache-dir supabase paho-mqtt

CMD ["python", "-u", "mqtt_domain.py"]