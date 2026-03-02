import { Client } from 'https://deno.land/x/mqtt/deno/mod.ts';
import { createClient } from 'jsr:@supabase/supabase-js@2'

Deno.serve(async (req) => {
  try {
    const body = await req.json();

    // Authenticate the caller via their JWT
    const supabase = createClient(
      Deno.env.get("SUPABASE_URL")!,
      Deno.env.get('SUPABASE_ANON_KEY')!,
      { global: { headers: { Authorization: req.headers.get('Authorization')! } } }
    );

    // Verify the user is an admin
    const { data: { user }, error: authError } = await supabase.auth.getUser();
    if (authError || !user) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), {
        status: 401, headers: { 'Content-Type': 'application/json' },
      });
    }

    // Service-role client for cross-table queries
    const adminClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!,
    );

    // Look up the target device
    const { data: device, error: deviceError } = await supabase
      .from("embeddeds")
      .select("id, company, status")
      .eq("id", body.device_id)
      .single();

    if (deviceError || !device) {
      return new Response(JSON.stringify({ error: 'Device not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json' },
      });
    }

    // Look up the firmware version
    const { data: firmware, error: fwError } = await supabase
      .from("firmware_versions")
      .select("id, file_path, version_label")
      .eq("id", body.firmware_id)
      .single();

    if (fwError || !firmware) {
      return new Response(JSON.stringify({ error: 'Firmware version not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json' },
      });
    }

    // Get the device passkey (needed for MQTT topic, fetched via admin client)
    const { data: deviceFull, error: deviceFullError } = await adminClient
      .from("embeddeds")
      .select("company")
      .eq("id", body.device_id)
      .single();

    if (deviceFullError || !deviceFull) {
      return new Response(JSON.stringify({ error: 'Device lookup failed' }), {
        status: 500, headers: { 'Content-Type': 'application/json' },
      });
    }

    // Construct the public download URL for the firmware binary
    // Use SUPABASE_PUBLIC_URL so the ESP32 device can reach it (not the internal Docker hostname)
    const publicUrl = Deno.env.get("PUBLIC_SUPABASE_URL") || Deno.env.get("SUPABASE_PUBLIC_URL") || Deno.env.get("SUPABASE_URL")!;
    const downloadUrl = `${publicUrl}/storage/v1/object/public/firmware/${firmware.file_path}`;

    // Publish OTA command to the device's MQTT topic
    const mqttHost = Deno.env.get("MQTT_HOST") || "mqtt.vmflow.xyz";
    const client = new Client({ url: `mqtt://${mqttHost}` });
    await client.connect();

    const otaTopic = `/${deviceFull.company}/${device.id}/ota`;
    const payload = JSON.stringify({ url: downloadUrl });

    await client.publish(otaTopic, new TextEncoder().encode(payload), { qos: 1 });
    await client.disconnect();

    // Record the OTA trigger in the database
    await adminClient
      .from("ota_updates")
      .insert({
        embedded_id: device.id,
        firmware_version_id: firmware.id,
        triggered_by: user.id,
        status: 'triggered',
      });

    return new Response(JSON.stringify({
      status: device.status,
      firmware: firmware.version_label,
      topic: otaTopic,
    }), {
      headers: { 'Content-Type': 'application/json' },
    });

  } catch (err) {
    return new Response(JSON.stringify({ message: err?.message ?? err }), {
      headers: { 'Content-Type': 'application/json' },
      status: 500,
    });
  }
});
