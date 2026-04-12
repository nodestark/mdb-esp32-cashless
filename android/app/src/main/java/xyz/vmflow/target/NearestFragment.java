package xyz.vmflow.target;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.location.Location;
import android.location.LocationManager;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.util.Base64;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.AutoCompleteTextView;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.polidea.rxandroidble3.RxBleClient;
import com.polidea.rxandroidble3.RxBleConnection;
import com.polidea.rxandroidble3.RxBleDevice;
import com.polidea.rxandroidble3.scan.ScanSettings;

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

import io.reactivex.rxjava3.core.Observable;
import io.reactivex.rxjava3.disposables.Disposable;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import xyz.vmflow.BuildConfig;
import xyz.vmflow.R;

public class NearestFragment extends Fragment {

    private static final String TAG = "NearestFragment";

    private static final UUID WRITE_CHARACTERISTIC_UUID = UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002");

    // BLE protocol commands (host → device)
    private static final byte CMD_BEGIN_SESSION  = 0x02;
    private static final byte CMD_CANCEL_SESSION = 0x04;
    private static final byte CMD_SEND_SSID      = 0x06;
    private static final byte CMD_SEND_PASSWORD  = 0x07;

    // BLE protocol events (device → host)
    private static final byte EVT_VEND_REQUEST    = 0x0a;
    private static final byte EVT_VEND_SUCCESS    = 0x0b;
    private static final byte EVT_VEND_FAILURE    = 0x0c;
    private static final byte EVT_SESSION_COMPLETE = 0x0d;

    private final List<RxBleDevice> mListRxBleDevices = new ArrayList<>();
    private final BleDeviceAdapter bleDeviceAdapter = new BleDeviceAdapter();
    private SwipeRefreshLayout mSwipeRefreshLayout;
    private View mProgressBar;

    private Disposable bleConnectionDisposable;
    private Disposable bleWifiDisposable;

    class BleDeviceViewHolder extends RecyclerView.ViewHolder {

        TextView viewText;
        ImageButton btnSetPassword;

        public BleDeviceViewHolder(@NonNull View itemView) {
            super(itemView);

            viewText = itemView.findViewById(R.id.textViewNearest);
            btnSetPassword = itemView.findViewById(R.id.btnSetPassword);
        }
    }

    class BleDeviceAdapter extends RecyclerView.Adapter<BleDeviceViewHolder> {

        private RxBleConnection mRxBleConnection;

        @Override
        public BleDeviceViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_nearest_layout, parent, false);
            BleDeviceViewHolder holder = new BleDeviceViewHolder(view);

            view.setOnClickListener(v -> {
                int pos = holder.getAdapterPosition();
                if (pos != RecyclerView.NO_POSITION) {

                    RxBleDevice device = mListRxBleDevices.get(pos);

                    AlertDialog.Builder builder = new AlertDialog.Builder(getContext());
                    builder.setNegativeButton("Cancel", (cancelDialog, which) -> {

                        if (mRxBleConnection != null) {
                            Disposable dis = mRxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, new byte[]{CMD_CANCEL_SESSION}).toObservable().subscribe(ignored -> {
                            }, throwable -> {
                            }, () -> {
                                bleConnectionDisposable.dispose();

                                if (getActivity() != null) getActivity().runOnUiThread(() -> {
                                    mProgressBar.setVisibility(View.VISIBLE);
                                    mListRxBleDevices.clear();
                                    bleDeviceAdapter.notifyDataSetChanged();
                                });

                                ExecutorService executor = Executors.newSingleThreadExecutor();
                                executor.execute(NearestFragment.this::fetchNearestData);
                                executor.shutdown();
                            });
                        }

                    });

                    builder.setTitle("Send Credit (BLE)");
                    builder.setMessage("Connecting...");
                    builder.setCancelable(false);

                    AlertDialog dialog = builder.create();
                    dialog.show();

                    bleConnectionDisposable = device.establishConnection(false)
                            .doOnDispose(() -> dialog.dismiss())
                            .flatMap(rxBleConnection -> rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, new byte[]{CMD_BEGIN_SESSION}).toObservable()
                                    .flatMap(initResult -> {
                                        Log.d(TAG, "Session started");

                                        if (getActivity() != null) getActivity().runOnUiThread(() -> dialog.setMessage("Please select a product on the machine."));

                                        mRxBleConnection = rxBleConnection;

                                        return rxBleConnection.setupNotification(WRITE_CHARACTERISTIC_UUID)
                                                .flatMap(notificationObservable ->
                                                        notificationObservable.flatMap(notification -> {

                                                            Log.d(TAG, "Received: " + Integer.toHexString(notification[0]));

                                                            if (notification[0] == EVT_SESSION_COMPLETE) {
                                                                bleConnectionDisposable.dispose();

                                                                if (getActivity() != null) getActivity().runOnUiThread(() -> {
                                                                    dialog.setMessage("Session finished.");
                                                                    mProgressBar.setVisibility(View.VISIBLE);
                                                                    mListRxBleDevices.clear();
                                                                    bleDeviceAdapter.notifyDataSetChanged();
                                                                });

                                                                ExecutorService executor = Executors.newSingleThreadExecutor();
                                                                executor.execute(NearestFragment.this::fetchNearestData);
                                                                executor.shutdown();
                                                            }

                                                            if (notification[0] == EVT_VEND_FAILURE) {
                                                                if (getActivity() != null) getActivity().runOnUiThread(() -> dialog.setMessage("Purchase failed. Please try again."));
                                                            }

                                                            if (notification[0] == EVT_VEND_SUCCESS) {
                                                                if (getActivity() != null) getActivity().runOnUiThread(() -> dialog.setMessage("Product dispensed successfully!"));
                                                            }

                                                            if (notification[0] == EVT_VEND_REQUEST) {
                                                                if (getActivity() != null) getActivity().runOnUiThread(() -> dialog.setMessage("Processing payment..."));

                                                                boolean retry;
                                                                do {
                                                                    retry = false;

                                                                    try {

                                                                        LocationManager locationManager = (LocationManager) getContext().getSystemService(Context.LOCATION_SERVICE);
                                                                        Location location = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);

                                                                        SharedPreferences prefs = getContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);

                                                                        JSONObject body = new JSONObject();
                                                                        body.put("payload", Base64.encodeToString(notification, Base64.NO_WRAP));
                                                                        body.put("subdomain", device.getName().split("\\.")[0]);

                                                                        if (location != null) {
                                                                            body.put("lat", location.getLatitude());
                                                                            body.put("lng", location.getLongitude());
                                                                        }

                                                                        RequestBody requestBody = RequestBody.create(body.toString(), MediaType.parse("application/json"));

                                                                        JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                                                                        Request request = new Request.Builder().url(BuildConfig.SUPABASE_URL + "/functions/v1/request-credit")
                                                                                .post(requestBody)
                                                                                .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                                                                                .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                                                                                .addHeader("Content-Type", "application/json")
                                                                                .build();

                                                                        Response response = new OkHttpClient().newCall(request).execute();
                                                                        if (response.isSuccessful()) {

                                                                            JSONObject responseBody = new JSONObject(response.body().string());
                                                                            byte[] payload = Base64.decode(responseBody.getString("payload"), Base64.NO_WRAP);

                                                                            return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payload).toObservable();

                                                                        } else if (response.code() == 401) {

                                                                            RequestBody refreshRequestBody = RequestBody.create(jsonAuth.toString(), MediaType.parse("application/json"));

                                                                            Request refreshRequest = new Request.Builder().url(BuildConfig.SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                                                                                    .post(refreshRequestBody)
                                                                                    .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                                                                                    .addHeader("Content-Type", "application/json")
                                                                                    .build();

                                                                            Response refreshResponse = new OkHttpClient().newCall(refreshRequest).execute();
                                                                            if (refreshResponse.isSuccessful()) {

                                                                                SharedPreferences.Editor editor = prefs.edit();
                                                                                editor.putString("auth_json", refreshResponse.body().string());
                                                                                editor.apply();

                                                                                retry = true;
                                                                            } else {
                                                                                redirectToLogin();
                                                                            }
                                                                        }

                                                                    } catch (JSONException | IOException e) {
                                                                        Log.e(TAG, "Payment request error", e);
                                                                    }

                                                                } while (retry);

                                                                if (getActivity() != null) getActivity().runOnUiThread(() -> dialog.setMessage("Payment failed."));

                                                                return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, new byte[]{CMD_CANCEL_SESSION}).toObservable();
                                                            }
                                                            return Observable.empty();
                                                        })
                                                );
                                    })
                            )
                            .retry(2)
                            .subscribe(
                                    result -> Log.d(TAG, "Write/response completed"),
                                    throwable -> Log.e(TAG, "BLE error", throwable),
                                    () -> Log.d(TAG, "BLE stream complete")
                            );

                }
            });

            holder.btnSetPassword.setOnClickListener(v -> {

                View dialogView = LayoutInflater.from(getContext()).inflate(R.layout.dialog_wifi_settings, null);

                AutoCompleteTextView editTextSsid = dialogView.findViewById(R.id.editTextSsid);
                EditText editTextPassword = dialogView.findViewById(R.id.editTextPassword);

                WifiManager wifiManager = (WifiManager) getActivity().getApplicationContext().getSystemService(Context.WIFI_SERVICE);

                if (wifiManager != null) {

                    wifiManager.startScan();
                    List<android.net.wifi.ScanResult> results = wifiManager.getScanResults();

                    List<String> ssidList = new ArrayList<>();
                    for (android.net.wifi.ScanResult result : results) {
                        if (result.SSID != null && !result.SSID.isEmpty() && !ssidList.contains(result.SSID)) {
                            ssidList.add(result.SSID);
                        }
                    }

                    ArrayAdapter<String> adapter = new ArrayAdapter<>(getActivity(), android.R.layout.simple_dropdown_item_1line, ssidList);
                    editTextSsid.setAdapter(adapter);
                    editTextSsid.setThreshold(1);
                }

                AlertDialog.Builder builder = new AlertDialog.Builder(getContext());
                builder.setTitle("Wi-Fi Settings");
                builder.setView(dialogView);

                builder.setNegativeButton("Cancel", (dialog, which) -> dialog.dismiss());
                builder.setPositiveButton("Send", (dialog, which) -> {

                    String ssid = editTextSsid.getText().toString().trim();
                    String password = editTextPassword.getText().toString().trim();

                    if (ssid.isEmpty()) {
                        Toast.makeText(getContext(), "Select SSID", Toast.LENGTH_SHORT).show();
                        return;
                    }

                    byte[] payloadSsid = new byte[22];
                    {
                        byte[] arrSsid = ssid.getBytes(StandardCharsets.UTF_8);

                        payloadSsid[0] = CMD_SEND_SSID;
                        System.arraycopy(arrSsid, 0, payloadSsid, 1, arrSsid.length);

                        payloadSsid[1 + arrSsid.length] = 0x00;
                    }

                    byte[] payloadPswd = new byte[63];
                    {
                        byte[] arrPswd = password.getBytes(StandardCharsets.UTF_8);

                        payloadPswd[0] = CMD_SEND_PASSWORD;
                        System.arraycopy(arrPswd, 0, payloadPswd, 1, arrPswd.length);

                        payloadPswd[1 + arrPswd.length] = 0x00;
                    }

                    RxBleDevice rxBleDevice = mListRxBleDevices.get(holder.getAdapterPosition());
                    bleWifiDisposable = rxBleDevice.establishConnection(false)
                            .flatMap(rxBleConnection -> {
                                return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadSsid)
                                        .timeout(3, TimeUnit.SECONDS)
                                        .flatMap(writeResult -> {
                                            Log.d(TAG, "SSID written OK");
                                            return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadPswd).timeout(3, TimeUnit.SECONDS);
                                        }).toObservable();
                            })
                            .retry(2)
                            .subscribe(
                                    writeResult -> Log.d(TAG, "Password written OK"),
                                    error -> Log.e(TAG, "Wi-Fi config write error", error),
                                    () -> {}
                            );
                });

                AlertDialog dialog = builder.create();
                dialog.show();
            });

            return holder;
        }

        @Override
        public void onBindViewHolder(@NonNull BleDeviceViewHolder holder, int position) {

            RxBleDevice rxBleDevice = mListRxBleDevices.get(position);
            holder.viewText.setText(rxBleDevice.getName());
        }

        @Override
        public int getItemCount() {
            return mListRxBleDevices.size();
        }
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        mProgressBar = view.findViewById(R.id.progressBar);

        RecyclerView recyclerView = view.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter(bleDeviceAdapter);

        mSwipeRefreshLayout = view.findViewById(R.id.swipeRefresh);
        mSwipeRefreshLayout.setOnRefreshListener(() -> {

            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(this::fetchNearestData);
            executor.shutdown();
        });
    }

    private void fetchNearestData() {

        RxBleClient rxBleClient = RxBleClient.create(getContext());

        mListRxBleDevices.clear();
        Disposable scanDisposable = rxBleClient.scanBleDevices(new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build())
                .take(3, TimeUnit.SECONDS)
                .filter(scanResult -> scanResult.getBleDevice().getName() != null
                        && !scanResult.getBleDevice().getName().isEmpty()
                        && scanResult.getBleDevice().getName().endsWith(".vmflow.xyz")
                        && !scanResult.getBleDevice().getName().equals("0.vmflow.xyz"))
                .distinct(scanResult -> scanResult.getBleDevice().getMacAddress())
                .subscribe(scanResult -> {
                    Log.d(TAG, scanResult.getBleDevice().getName());
                    mListRxBleDevices.add(scanResult.getBleDevice());
                }, error -> {
                    Log.e(TAG, "BLE scan error", error);
                }, () -> {

                    if (getActivity() != null)
                        getActivity().runOnUiThread(() -> {

                            bleDeviceAdapter.notifyDataSetChanged();
                            mSwipeRefreshLayout.setRefreshing(false);
                            mProgressBar.setVisibility(View.GONE);

                            Log.d(TAG, "Scan complete");
                        });
                });
    }

    @Override
    public void onStart() {
        super.onStart();

        mProgressBar.setVisibility(View.VISIBLE);

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(this::fetchNearestData);
        executor.shutdown();
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        if (bleConnectionDisposable != null && !bleConnectionDisposable.isDisposed()) {
            bleConnectionDisposable.dispose();
        }
        if (bleWifiDisposable != null && !bleWifiDisposable.isDisposed()) {
            bleWifiDisposable.dispose();
        }
    }

    private void redirectToLogin() {
        if (getActivity() == null) return;
        getContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE)
                .edit().remove("auth_json").apply();
        getActivity().runOnUiThread(() -> {
            Intent intent = new Intent(getActivity(), LoginActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(intent);
        });
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_nearest, container, false);
    }
}
