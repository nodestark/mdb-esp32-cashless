package xyz.vmflow.target;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.AutoCompleteTextView;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.floatingactionbutton.FloatingActionButton;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Phaser;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class MainActivity extends AppCompatActivity {

    private static final UUID SERVICE_UUID = UUID.fromString("b2bbc642-46da-11ed-b878-0242ac120002");
    private static final UUID WRITE_CHARACTERISTIC_UUID = UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002");

    private final List<JSONObject> mListEmbedded = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    private static final String SUPABASE_URL = "https://supabase.vmflow.xyz";
    private static final String SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4";
    private BluetoothAdapter mBluetoothAdapter;
    private ActivityResultLauncher<Intent> mEnableBluetoothLauncher;

    private Phaser blePhaser = new Phaser(1);
    private BluetoothGattCallback mBluetoothGattCallback;

    class ViewHolder_ extends RecyclerView.ViewHolder {

        TextView deviceNameText;
        ImageButton btnSetPassword;

        ImageButton btnSendCredit;

        public ViewHolder_(@NonNull View itemView) {
            super(itemView);

            deviceNameText = itemView.findViewById(R.id.textViewDeviceAlias);
            btnSetPassword = itemView.findViewById(R.id.btnSetPassword);
            btnSendCredit = itemView.findViewById(R.id.btnSendCredit);
        }
    }

    class ItemAdapter_ extends RecyclerView.Adapter<ViewHolder_> {

        @NonNull
        @Override
        public ViewHolder_ onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_main_layout, parent, false);
            return new ViewHolder_(view);
        }

        @Override
        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

            JSONObject jsonObject = mListEmbedded.get(position);

            // "%06d" -> número inteiro com 6 dígitos, preenchido com zeros à esquerda
            String padded = null; // Exemplo: 000199
            try {
                padded = String.format("%06d", jsonObject.getInt("domain"));
            } catch (JSONException e) {
                e.printStackTrace();
            }

            holder.deviceNameText.setText("\uD83C\uDF6B Machine: " + padded);

            holder.btnSendCredit.setOnClickListener(v -> {

                AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this);

                View dialogView = LayoutInflater.from(MainActivity.this).inflate(R.layout.dialog_credit_settings, null);
                builder.setView(dialogView);

                builder.setTitle("Send Credit (MQTT)");
                builder.setNegativeButton("Cancel", (dialog, which) -> dialog.dismiss());
                builder.setPositiveButton("Send", (dialog, which) -> {

                    ProgressDialog progressDialog = new ProgressDialog(MainActivity.this);
                    progressDialog.setMessage("Carregando...");
                    progressDialog.setCancelable(false); // não fecha ao tocar fora
                    progressDialog.show();

                    ExecutorService executor = Executors.newSingleThreadExecutor();
                    executor.execute(() -> {

                        AutoCompleteTextView editTextAmount = dialogView.findViewById(R.id.editTextAmount);

                        Float amount = Float.valueOf( editTextAmount.getText().toString() );

                        boolean retry;
                        do {
                            retry = false; // controla se precisa repetir a requisição

                            try {
                                SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);

                                JSONObject jsonObjectSend= new JSONObject();
                                jsonObjectSend.put("amount", (int) (amount.floatValue() * 100) );
                                jsonObjectSend.put("domain", jsonObject.getInt("domain") );

                                RequestBody requestBody = RequestBody.create( jsonObjectSend.toString(), MediaType.parse("application/json") );

                                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                                Request request = new Request.Builder().url(SUPABASE_URL + "/functions/v1/send-credit")
                                        .post(requestBody)
                                        .addHeader("apikey", SUPABASE_KEY)
                                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                                        .addHeader("Content-Type", "application/json")
                                        .build();

                                Response response = new OkHttpClient().newCall(request).execute();
                                if(response.isSuccessful()){

                                    Log.d("send-credit", response.body().string());
                                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "Success", Toast.LENGTH_SHORT).show());

                                } else if (response.code() == 401) {

                                    RequestBody requestBody_ = RequestBody.create( jsonAuth.toString(), MediaType.parse("application/json") );

                                    Request request_ = new Request.Builder().url(SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                                            .post(requestBody_)
                                            .addHeader("apikey", SUPABASE_KEY)
                                            .addHeader("Content-Type", "application/json")
                                            .build();

                                    Response response_ = new OkHttpClient().newCall(request_).execute();
                                    if (response_.isSuccessful()) {

                                        SharedPreferences.Editor editor = prefs.edit();
                                        editor.putString("auth_json", response_.body().string());
                                        editor.apply();

                                        retry = true;
                                    }
                                } else {
                                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "Erro: " + response.code(), Toast.LENGTH_SHORT).show());
                                }

                            } catch (JSONException | IOException e) {
                                e.printStackTrace();
                            }

                        } while(retry);

                        progressDialog.dismiss();
                    } );
                });

                AlertDialog dialog = builder.create();
                dialog.show();
            });

            holder.btnSetPassword.setOnClickListener(v -> {

                View dialogView = LayoutInflater.from(MainActivity.this).inflate(R.layout.dialog_wifi_settings, null);

                AutoCompleteTextView editTextSsid = dialogView.findViewById(R.id.editTextSsid);
                EditText editTextPassword = dialogView.findViewById(R.id.editTextPassword);

                WifiManager wifiManager = (WifiManager) MainActivity.this.getApplicationContext().getSystemService(Context.WIFI_SERVICE);

                if (wifiManager != null) {
                    // Inicia o scan
                    wifiManager.startScan();
                    List<android.net.wifi.ScanResult> results = wifiManager.getScanResults();

                    List<String> ssidList = new ArrayList<>();
                    for (android.net.wifi.ScanResult result : results) {
                        if (result.SSID != null && !result.SSID.isEmpty() && !ssidList.contains(result.SSID)) {
                            ssidList.add(result.SSID);
                        }
                    }

                    // Preenche o AutoComplete
                    ArrayAdapter<String> adapter = new ArrayAdapter<>(MainActivity.this, android.R.layout.simple_dropdown_item_1line, ssidList);
                    editTextSsid.setAdapter(adapter);
                    editTextSsid.setThreshold(1); // sugere a partir de 1 caractere
                }

                AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this);
                builder.setTitle("Wi-Fi Settings");
                builder.setView(dialogView);

                builder.setPositiveButton("Send", (dialog, which) -> {
                    String ssid = editTextSsid.getText().toString().trim();
                    String password = editTextPassword.getText().toString().trim();

                    if (ssid.isEmpty() ) {
                        Toast.makeText(MainActivity.this, "...SSID", Toast.LENGTH_SHORT).show();
                        return;
                    }

                    BluetoothLeScanner bluetoothLeScanner = mBluetoothAdapter.getBluetoothLeScanner();

                    ProgressDialog progressDialog = new ProgressDialog(MainActivity.this);
                    progressDialog.setMessage("Loading...");
                    progressDialog.setCancelable(false); // não fecha ao tocar fora

                    progressDialog.show();

                    AtomicBoolean deviceFound = new AtomicBoolean(false);

                    ScanCallback currentScanCallback = new ScanCallback() {

                        @Override
                        public void onScanResult(int callbackType, ScanResult result) {

                            deviceFound.set(true);

                            bluetoothLeScanner.stopScan(this);

                            ExecutorService executor = Executors.newSingleThreadExecutor();
                            executor.execute(() -> {

                                BluetoothDevice bluetoothDevice = result.getDevice();
                                Log.d("ble_", bluetoothDevice.getName());

                                BluetoothGatt bluetoothGatt = bluetoothDevice.connectGatt(getApplicationContext(), false, mBluetoothGattCallback);

                                // wait onDescriptorWrite...
                                try {
                                    blePhaser.awaitAdvanceInterruptibly(blePhaser.getPhase(), 5, TimeUnit.SECONDS);
                                } catch (InterruptedException | TimeoutException e) {

                                    bluetoothGatt.disconnect();
                                    progressDialog.dismiss();

                                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "Error", Toast.LENGTH_SHORT).show());

                                    return;
                                }
                                Log.d("ble_", "...wait onDescriptorWrite");

                                BluetoothGattService service =  bluetoothGatt.getService( SERVICE_UUID );
                                if (service == null) {
                                    Log.e("ble_", "Serviço não encontrado: " + SERVICE_UUID);

                                    bluetoothGatt.disconnect();
                                    progressDialog.dismiss();

                                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "Error", Toast.LENGTH_SHORT).show());

                                    return;
                                }

                                BluetoothGattCharacteristic writeChar = service.getCharacteristic( WRITE_CHARACTERISTIC_UUID );

                                {
                                    byte[] payload = new byte[22];
                                    byte[] arrSsid = ssid.getBytes(StandardCharsets.UTF_8);

                                    payload[0] = 0x06;
                                    System.arraycopy(arrSsid, 0, payload, 1, arrSsid.length);

                                    payload[1 + arrSsid.length] = 0x00;

                                    writeChar.setValue(payload);
                                    bluetoothGatt.writeCharacteristic(writeChar);

                                    // wait onCharacteristicWrite...
                                    blePhaser.awaitAdvance(blePhaser.getPhase());
                                    Log.d("ble_", "...wait onCharacteristicWrite");
                                }

                                {
                                    byte[] payload = new byte[22];
                                    byte[] arrPswd = password.getBytes(StandardCharsets.UTF_8);

                                    payload[0] = 0x07;
                                    System.arraycopy(arrPswd, 0, payload, 1, arrPswd.length);

                                    payload[1 + arrPswd.length] = 0x00;

                                    writeChar.setValue(payload);
                                    bluetoothGatt.writeCharacteristic(writeChar);

                                    // wait onCharacteristicWrite...
                                    blePhaser.awaitAdvance(blePhaser.getPhase());
                                    Log.d("ble_", "...wait onCharacteristicWrite");
                                }

                                runOnUiThread(() -> Toast.makeText(MainActivity.this, "Success", Toast.LENGTH_SHORT).show());

                                bluetoothGatt.disconnect();
                                progressDialog.dismiss();
                            });
                        }

                        @Override
                        public void onScanFailed(int errorCode) {
                            Log.e("ble_", "Erro no scan: " + errorCode);
                        }
                    };

                    try {
                        String deviceName = String.format("%d.vmflow.xyz", jsonObject.getInt("domain"));

                        Log.d("ble_", deviceName);

                        ScanFilter filter = new ScanFilter.Builder().setDeviceName( deviceName ).build();
                        List<ScanFilter> filters = new ArrayList<>();
                        filters.add(filter);

                        bluetoothLeScanner.startScan(filters, new ScanSettings.Builder().build(), currentScanCallback);

                    } catch (JSONException e) {
                        throw new RuntimeException(e);
                    }

                    new Handler().postDelayed(() -> {

                        if(deviceFound.get())
                            return;

                        bluetoothLeScanner.stopScan(currentScanCallback);
                        progressDialog.dismiss();

                    }, 3000);

                });

                builder.setNegativeButton("Cancel", (dialog, which) -> dialog.dismiss());

                AlertDialog dialog = builder.create();
                dialog.show();
            });
        }

        @Override
        public int getItemCount() {
            return mListEmbedded.size();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        RecyclerView recyclerView = findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        recyclerView.setAdapter( itemAdapter_ );

        FloatingActionButton fab = findViewById(R.id.fabAdd);

        BluetoothManager bluetoothManager = getSystemService(BluetoothManager.class);

        mBluetoothAdapter = bluetoothManager.getAdapter();

        mEnableBluetoothLauncher = registerForActivityResult(
                new ActivityResultContracts.StartActivityForResult(), result -> {
                    if (result.getResultCode() == Activity.RESULT_OK) {
                        // Bluetooth ON

                        fab.setVisibility(View.VISIBLE);
                    }
                }
        );

        mBluetoothGattCallback = new BluetoothGattCallback() {

            @Override
            public void onCharacteristicChanged(@NonNull BluetoothGatt gatt, @NonNull BluetoothGattCharacteristic characteristic, @NonNull byte[] value) {
                Log.d("ble_", "onCharacteristicChanged");
            }

            @Override
            public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {
                Log.d("ble_", "onDescriptorWrite");
                blePhaser.arrive();
            }

            @Override
            public void onServicesDiscovered(BluetoothGatt gatt, int status) {

                if (status == BluetoothGatt.GATT_SUCCESS) {

                    BluetoothGattService service = gatt.getService( SERVICE_UUID );
                    if (service != null) {
                        Log.d("ble_", "onServicesDiscovered");

                        BluetoothGattCharacteristic writeChar = service.getCharacteristic( WRITE_CHARACTERISTIC_UUID );
                        if (writeChar != null) {
                            Log.d("ble_", "onCharacteristicDiscovered");

                            gatt.setCharacteristicNotification(writeChar, true);

                            BluetoothGattDescriptor descriptor = writeChar.getDescriptor( UUID.fromString("00002902-0000-1000-8000-00805f9b34fb") /*Default client UUID*/ );

                            if (descriptor != null) {
                                descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                                gatt.writeDescriptor(descriptor);
                            }
                        }
                    }
                } else {
                    Log.d("ble_", "Falha ao descobrir serviços, status: " + status);
                }
            }

            @Override
            public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
                Log.d("ble_", "onCharacteristicWrite");
                blePhaser.arrive();
            }

            @Override
            public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    Log.d("ble_", "Conectado");
                    gatt.discoverServices();

                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    Log.d("ble_", "Desconectado");
                }
            }
        };

        BluetoothLeScanner bluetoothLeScanner = mBluetoothAdapter.getBluetoothLeScanner();

        fab.setOnClickListener(view -> {

            ProgressDialog progressDialog = new ProgressDialog(this);
            progressDialog.setMessage("Carregando...");
            progressDialog.setCancelable(false); // não fecha ao tocar fora

            progressDialog.show();

            AtomicBoolean deviceFound = new AtomicBoolean(false);

            ScanCallback currentScanCallback = new ScanCallback() {

                @Override
                public void onScanResult(int callbackType, ScanResult result) {

                    deviceFound.set(true);

                    bluetoothLeScanner.stopScan(this);

                    ExecutorService executor = Executors.newSingleThreadExecutor();
                    executor.execute(() -> {

                        BluetoothDevice bluetoothDevice = result.getDevice();
                        Log.d("ble_", bluetoothDevice.getName());

                        BluetoothGatt bluetoothGatt = bluetoothDevice.connectGatt(getApplicationContext(), false, mBluetoothGattCallback);

                        // wait onDescriptorWrite...
                        try {
                            blePhaser.awaitAdvanceInterruptibly(blePhaser.getPhase(), 5, TimeUnit.SECONDS);
                        } catch (InterruptedException | TimeoutException e) {

                            bluetoothGatt.disconnect();
                            progressDialog.dismiss();

                            return;
                        }
                        Log.d("ble_", "...wait onDescriptorWrite");

                        BluetoothGattService service =  bluetoothGatt.getService( SERVICE_UUID );
                        if (service == null) {
                            Log.e("ble_", "Serviço não encontrado: " + SERVICE_UUID);

                            bluetoothGatt.disconnect();
                            progressDialog.dismiss();

                            return;
                        }

                        BluetoothGattCharacteristic writeChar = service.getCharacteristic( WRITE_CHARACTERISTIC_UUID );

                        boolean retry;
                        do {
                            retry = false; // controla se precisa repetir a requisição

                            try {
                                SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);

                                JSONObject jsonObject= new JSONObject();
                                jsonObject.put("mac", bluetoothDevice.getAddress());

                                RequestBody requestBody = RequestBody.create( jsonObject.toString(), MediaType.parse("application/json") );

                                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                                Request request = new Request.Builder().url(SUPABASE_URL + "/rest/v1/embedded")
                                        .post(requestBody)
                                        .addHeader("apikey", SUPABASE_KEY)
                                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                                        .addHeader("Content-Type", "application/json")
                                        .addHeader("Prefer", "return=representation")
                                        .build();

                                Response response = new OkHttpClient().newCall(request).execute();
                                if(response.isSuccessful()){

                                    JSONArray jsonArray = new JSONArray(response.body().string());
                                    JSONObject jsonNewEmbedded = jsonArray.getJSONObject(0);

                                    byte[] payload = new byte[22];

                                    {
                                        byte[] arrAlias = jsonNewEmbedded.getString("domain").getBytes(StandardCharsets.UTF_8);

                                        payload[0] = 0x00;
                                        System.arraycopy(arrAlias, 0, payload, 1, arrAlias.length);

                                        payload[1 + arrAlias.length] = 0x00;

                                        writeChar.setValue(payload);
                                        bluetoothGatt.writeCharacteristic(writeChar);

                                        // wait onCharacteristicWrite...
                                        blePhaser.awaitAdvance(blePhaser.getPhase());
                                        Log.d("ble_", "...wait onCharacteristicWrite");
                                    }

                                    {
                                        byte[] arrXorkey = jsonNewEmbedded.getString("vernam").getBytes(StandardCharsets.UTF_8);

                                        payload[0] = 0x01;
                                        System.arraycopy(arrXorkey, 0, payload, 1, arrXorkey.length);

                                        payload[1 + arrXorkey.length] = 0x00;

                                        writeChar.setValue(payload);
                                        bluetoothGatt.writeCharacteristic(writeChar);

                                        // wait onCharacteristicWrite...
                                        blePhaser.awaitAdvance(blePhaser.getPhase());
                                        Log.d("ble_", "...wait onCharacteristicWrite");
                                    }

                                    mListEmbedded.add(jsonNewEmbedded);
                                    runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());

                                    AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this);

                                    builder.setTitle("Registered!");
                                    builder.setMessage(jsonNewEmbedded.getString("domain"));

                                    builder.setPositiveButton("Ok", new DialogInterface.OnClickListener() {
                                        @Override
                                        public void onClick(DialogInterface dialogInterface, int i) {
                                            dialogInterface.dismiss();
                                        }
                                    });

                                    runOnUiThread(() -> {
                                        builder.create().show();
                                    });

                                } else if (response.code() == 401) {

                                    RequestBody requestBody_ = RequestBody.create( jsonAuth.toString(), MediaType.parse("application/json") );

                                    Request request_ = new Request.Builder().url(SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                                            .post(requestBody_)
                                            .addHeader("apikey", SUPABASE_KEY)
                                            .addHeader("Content-Type", "application/json")
                                            .build();

                                    Response response_ = new OkHttpClient().newCall(request_).execute();
                                    if (response_.isSuccessful()) {

                                        SharedPreferences.Editor editor = prefs.edit();
                                        editor.putString("auth_json", response_.body().string());
                                        editor.apply();

                                        retry = true;
                                    }
                                } else {
                                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "Erro: " + response.code(), Toast.LENGTH_SHORT).show());
                                }

                            } catch (JSONException | IOException e) {
                                e.printStackTrace();
                            }

                        } while(retry);

                        bluetoothGatt.disconnect();
                        progressDialog.dismiss();
                    });
                }

                @Override
                public void onScanFailed(int errorCode) {
                    Log.e("ble_", "Erro no scan: " + errorCode);
                }
            };

            ScanFilter filter = new ScanFilter.Builder().setDeviceName("0.vmflow.xyz").build();
            List<ScanFilter> filters = new ArrayList<>();
            filters.add(filter);

            bluetoothLeScanner.startScan(filters, new ScanSettings.Builder().build(), currentScanCallback);

            new Handler().postDelayed(() -> {

                if(deviceFound.get())
                    return;

                bluetoothLeScanner.stopScan(currentScanCallback);
                progressDialog.dismiss();

            }, 3000);
        });

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {

            ProgressBar progressBar = findViewById(R.id.progressBar);

            boolean retry;
            do {
                retry = false; // controla se precisa repetir a requisição

                try {
                    SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);

                    JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                    Request request = new Request.Builder()
                            .url(SUPABASE_URL + "/rest/v1/embedded")
                            .addHeader("apikey", SUPABASE_KEY)
                            .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                            .addHeader("Content-Type", "application/json")
                            .build();

                    Response response = new OkHttpClient().newCall(request).execute();
                    if(response.isSuccessful()){

                        try {
                            JSONArray jsonArray = new JSONArray(response.body().string());

                            mListEmbedded.clear();
                            for (int i = 0; i < jsonArray.length(); i++) {
                                mListEmbedded.add(jsonArray.getJSONObject(i));
                            }

                            runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());

                        } catch (JSONException e) {
                            throw new RuntimeException(e);
                        }

                    } else if (response.code() == 401) {

                        RequestBody requestBody = RequestBody.create( jsonAuth.toString(), MediaType.parse("application/json") );

                        Request request_ = new Request.Builder().url(SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                                .post(requestBody)
                                .addHeader("apikey", SUPABASE_KEY)
                                .addHeader("Content-Type", "application/json")
                                .build();

                        Response response_ = new OkHttpClient().newCall(request_).execute();
                        if (response_.isSuccessful()) {

                            SharedPreferences.Editor editor = prefs.edit();
                            editor.putString("auth_json", response_.body().string());
                            editor.apply();

                            retry = true;
                        }
                    } else {
                        runOnUiThread(() -> Toast.makeText(MainActivity.this, "Erro: " + response.code(), Toast.LENGTH_SHORT).show());
                    }

                } catch (JSONException | IOException e) {
                    e.printStackTrace();
                }

            } while(retry);

            runOnUiThread(() -> progressBar.setVisibility(View.GONE));
        });
    }

    @Override
    protected void onStart() {
        super.onStart();

        if (hasBluetoothPermissions()) {
            checkAndEnableBluetooth();
        } else {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT}, 101);
            } else {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.BLUETOOTH, Manifest.permission.BLUETOOTH_ADMIN, Manifest.permission.ACCESS_FINE_LOCATION}, 101);
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == 101) {
            checkAndEnableBluetooth();
        }
    }

    private boolean hasBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
                    && ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
        } else {
            return ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH) == PackageManager.PERMISSION_GRANTED
                    && ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_ADMIN) == PackageManager.PERMISSION_GRANTED
                    && ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
        }
    }

    private void checkAndEnableBluetooth() {

        if(!mBluetoothAdapter.isEnabled()){
            mEnableBluetoothLauncher.launch(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
        } else {
            FloatingActionButton fab = findViewById(R.id.fabAdd);
            fab.setVisibility(View.VISIBLE);
        }
    }
}