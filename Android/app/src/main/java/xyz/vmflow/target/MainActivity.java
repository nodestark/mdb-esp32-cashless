package xyz.vmflow.target;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
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
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.google.android.material.floatingactionbutton.FloatingActionButton;
import com.polidea.rxandroidble3.RxBleClient;
import com.polidea.rxandroidble3.RxBleDevice;
import com.polidea.rxandroidble3.scan.ScanFilter;
import com.polidea.rxandroidble3.scan.ScanResult;
import com.polidea.rxandroidble3.scan.ScanSettings;

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
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import io.reactivex.rxjava3.disposables.Disposable;
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
    private ActivityResultLauncher<Intent> mEnableBluetoothLauncher;

    private RxBleClient mRxBleClient;

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

            JSONObject jsonEmbedded = mListEmbedded.get(position);

            // "%06d" -> número inteiro com 6 dígitos, preenchido com zeros à esquerda
            String padded = null; // Exemplo: 000199
            try {
                padded = String.format("%06d", jsonEmbedded.getInt("subdomain"));
            } catch (JSONException e) {
                e.printStackTrace();
            }

            try {
                if ("online".equals(jsonEmbedded.getString("status"))) {
                    holder.deviceNameText.setText("\uD83D\uDFE2 Machine: " + padded);
                } else {
                    holder.deviceNameText.setText("\uD83D\uDD34 Machine: " + padded);
                }
            } catch (JSONException e) {
                throw new RuntimeException(e);
            }


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
                            retry = false;

                            try {
                                SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);

                                JSONObject jsonObjectSend= new JSONObject();
                                jsonObjectSend.put("amount", (int) (amount.floatValue() * 100) );
                                jsonObjectSend.put("subdomain", jsonEmbedded.getInt("subdomain") );

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

                String embeddedDomain = null;
                try {
                    embeddedDomain = String.format("%d.vmflow.xyz", jsonEmbedded.getInt("subdomain"));
                } catch (JSONException e) {
                    throw new RuntimeException(e);
                }

                ProgressDialog progressDialog = new ProgressDialog(MainActivity.this);
                progressDialog.setMessage("Connecting to " + embeddedDomain);
                progressDialog.setCancelable(false);

                progressDialog.show();

                Disposable disposable = mRxBleClient.scanBleDevices(new ScanSettings.Builder().build(), new ScanFilter.Builder().setDeviceName(embeddedDomain).build())
                        .firstElement()
                        .timeout(3, TimeUnit.SECONDS)
                        .doFinally(() -> {
                            progressDialog.dismiss();
                        })
                        .subscribe( MainActivity.this::rxBleSetWifi, th -> {

                            if (th instanceof TimeoutException) {
                                runOnUiThread(() -> Toast.makeText(MainActivity.this, "Embedded not found", Toast.LENGTH_SHORT).show());
                            }
                        });
            });
        }

        @Override
        public int getItemCount() {
            return mListEmbedded.size();
        }
    }

    private void rxBleSetWifi(ScanResult scanResult) {

        View dialogView = LayoutInflater.from(MainActivity.this).inflate(R.layout.dialog_wifi_settings, null);

        AutoCompleteTextView editTextSsid = dialogView.findViewById(R.id.editTextSsid);
        EditText editTextPassword = dialogView.findViewById(R.id.editTextPassword);

        WifiManager wifiManager = (WifiManager) MainActivity.this.getApplicationContext().getSystemService(Context.WIFI_SERVICE);

        if (wifiManager != null) {

            wifiManager.startScan();
            List<android.net.wifi.ScanResult> results = wifiManager.getScanResults();

            List<String> ssidList = new ArrayList<>();
            for (android.net.wifi.ScanResult result : results) {
                if (result.SSID != null && !result.SSID.isEmpty() && !ssidList.contains(result.SSID)) {
                    ssidList.add(result.SSID);
                }
            }

            ArrayAdapter<String> adapter = new ArrayAdapter<>(MainActivity.this, android.R.layout.simple_dropdown_item_1line, ssidList);
            editTextSsid.setAdapter(adapter);
            editTextSsid.setThreshold(1);
        }

        AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this);
        builder.setTitle("Wi-Fi Settings");
        builder.setView(dialogView);

        builder.setNegativeButton("Cancel", (dialog, which) -> dialog.dismiss());
        builder.setPositiveButton("Send", (dialog, which) -> {

            String ssid = editTextSsid.getText().toString().trim();
            String password = editTextPassword.getText().toString().trim();

            if (ssid.isEmpty() ) {
                Toast.makeText(MainActivity.this, "Select SSID", Toast.LENGTH_SHORT).show();
                return;
            }

            byte[] payloadSsid = new byte[22];
            {
                byte[] arrSsid = ssid.getBytes(StandardCharsets.UTF_8);

                payloadSsid[0] = 0x06;
                System.arraycopy(arrSsid, 0, payloadSsid, 1, arrSsid.length);

                payloadSsid[1 + arrSsid.length] = 0x00;
            }

            byte[] payloadPswd = new byte[22];
            {
                byte[] arrPswd = password.getBytes(StandardCharsets.UTF_8);

                payloadPswd[0] = 0x07;
                System.arraycopy(arrPswd, 0, payloadPswd, 1, arrPswd.length);

                payloadPswd[1 + arrPswd.length] = 0x00;
            }

            RxBleDevice rxBleDevice = scanResult.getBleDevice();

            final Disposable[] disposableRef = new Disposable[1];

            disposableRef[0] = rxBleDevice.establishConnection(false)
                    .flatMap(rxBleConnection -> {
                        return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadSsid)
                                .flatMap(bytes -> {
                                    Log.d("rxBle_", "Write 1 OK: " + new String(bytes));
                                    return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadPswd);
                                }).toObservable();
                    })
                    .subscribe(
                            bytes -> Log.d("rxBle_", "Write 2 OK: " + new String(bytes)),
                            err -> Log.e("rxBle_", "Erro em algum write", err),
                            () -> disposableRef[0].dispose()
                    );
        });

        AlertDialog dialog = builder.create();
        dialog.show();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mRxBleClient = RxBleClient.create(this);

        RecyclerView recyclerView = findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        recyclerView.setAdapter( itemAdapter_ );

        FloatingActionButton fab = findViewById(R.id.fabAdd);

        mEnableBluetoothLauncher = registerForActivityResult(
                new ActivityResultContracts.StartActivityForResult(), result -> {
                    if (result.getResultCode() == Activity.RESULT_OK) {
                        // Bluetooth ON

                        fab.setVisibility(View.VISIBLE);
                    }
                }
        );

        fab.setOnClickListener(view -> {

            ProgressDialog progressDialog = new ProgressDialog(MainActivity.this);

            progressDialog.setMessage("Connecting to 0.vmflow.xyz");
            progressDialog.setCancelable(false); // não fecha ao tocar fora

            progressDialog.show();

            Disposable disposable = mRxBleClient.scanBleDevices(new com.polidea.rxandroidble3.scan.ScanSettings.Builder().build(), new com.polidea.rxandroidble3.scan.ScanFilter.Builder().setDeviceName("0.vmflow.xyz").build())
                    .firstElement()
                    .timeout(3, TimeUnit.SECONDS)
                    .doFinally(() -> progressDialog.dismiss())
                    .subscribe( MainActivity.this::rxBleInstall, th -> {

                        if (th instanceof TimeoutException) {
                            runOnUiThread(() -> Toast.makeText(MainActivity.this, "Embedded not found", Toast.LENGTH_SHORT).show());
                        }
                    });
        });

        SwipeRefreshLayout swipeRefreshLayout = findViewById(R.id.swipeRefresh);
        swipeRefreshLayout.setOnRefreshListener(new SwipeRefreshLayout.OnRefreshListener() {
            @Override
            public void onRefresh() {

                ExecutorService executor = Executors.newSingleThreadExecutor();
                executor.execute(() -> {

                    fetchEmbeddedData();

                    runOnUiThread(() -> swipeRefreshLayout.setRefreshing(false));
                });
            }
        });
    }

    private void fetchEmbeddedData() {

        boolean retry;
        do {
            retry = false; // controla se precisa repetir a requisição

            try {
                SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);

                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                Request request = new Request.Builder()
                        .url(SUPABASE_URL + "/rest/v1/embeddeds")
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
                        e.printStackTrace();
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
    }

    private void rxBleInstall(ScanResult scanResult) {
        Log.d("rxBle_", "rxBleInstall");

        RxBleDevice rxBleDevice = scanResult.getBleDevice();

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {

            boolean retry;
            do {
                retry = false;

                try {
                    SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);

                    JSONObject jsonObject= new JSONObject();
                    jsonObject.put("mac_address", rxBleDevice.getMacAddress());

                    RequestBody requestBody = RequestBody.create( jsonObject.toString(), MediaType.parse("application/json") );

                    JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                    Request request = new Request.Builder().url(SUPABASE_URL + "/rest/v1/embeddeds")
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

                        final Disposable[] disposableRef = new Disposable[1];

                        disposableRef[0] = rxBleDevice.establishConnection(false)
                                .flatMap(rxBleConnection -> {
                                    Log.d("rxBle_", "Conectado!");

                                    // --- Prepara payloads ---
                                    byte[] payloadSubdomain = new byte[22];
                                    {
                                        byte[] arrAlias = jsonNewEmbedded.getString("subdomain").getBytes(StandardCharsets.UTF_8);
                                        payloadSubdomain[0] = 0x00;
                                        System.arraycopy(arrAlias, 0, payloadSubdomain, 1, arrAlias.length);
                                        payloadSubdomain[1 + arrAlias.length] = 0x00;
                                    }

                                    byte[] payloadPasskey = new byte[22];
                                    {
                                        byte[] arrXorkey = jsonNewEmbedded.getString("passkey").getBytes(StandardCharsets.UTF_8);
                                        payloadPasskey[0] = 0x01;
                                        System.arraycopy(arrXorkey, 0, payloadPasskey, 1, arrXorkey.length);
                                        payloadPasskey[1 + arrXorkey.length] = 0x00;
                                    }

                                    return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadSubdomain)
                                            .flatMap(bytes -> {
                                                Log.d("rxBle_", "Write 1 OK: " + new String(bytes));
                                                return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadPasskey);
                                            }).toObservable();
                                })
                                .subscribe(
                                        bytes -> {
                                            Log.d("rxBle_", "Write 2 OK: " + new String(bytes));
                                        },
                                        err -> {
                                            Log.e("rxBle_", "Erro na conexão ou nos writes", err);

                                            runOnUiThread(() ->
                                                    Toast.makeText(MainActivity.this, "Erro BLE", Toast.LENGTH_SHORT).show()
                                            );
                                        }, () -> disposableRef[0].dispose()
                                );

                        mListEmbedded.add(jsonNewEmbedded);
                        runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());

                        AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this);
                        builder.setTitle("Registered!");
                        builder.setMessage(jsonNewEmbedded.getString("subdomain"));
                        builder.setPositiveButton("Ok", null );

                        runOnUiThread(() -> builder.create().show() );

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
        });
    }

    @Override
    protected void onStart() {
        super.onStart();

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            fetchEmbeddedData();

            ProgressBar progressBar = findViewById(R.id.progressBar);
            runOnUiThread(() -> progressBar.setVisibility(View.GONE) );
        });

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

        if(!getSystemService(BluetoothManager.class).getAdapter().isEnabled()){
            mEnableBluetoothLauncher.launch(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
        } else {
            FloatingActionButton fab = findViewById(R.id.fabAdd);
            fab.setVisibility(View.VISIBLE);
        }
    }
}