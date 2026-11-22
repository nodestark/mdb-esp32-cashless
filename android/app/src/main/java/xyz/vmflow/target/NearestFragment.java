package xyz.vmflow.target;

import android.app.AlertDialog;
import android.content.Context;
import android.location.Location;
import android.location.LocationManager;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
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
import java.util.concurrent.TimeUnit;

import io.reactivex.rxjava3.core.Observable;
import io.reactivex.rxjava3.core.Single;
import io.reactivex.rxjava3.disposables.Disposable;
import io.reactivex.rxjava3.schedulers.Schedulers;
import okhttp3.Request;
import okhttp3.Response;
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

    // No product picked within this time → cancel. The firmware drops an idle session after 60 s,
    // so this is only a fallback for when its SESSION COMPLETE never reaches the phone.
    private static final long IDLE_TIMEOUT_MS = 65_000;
    // After approval: dispensing can take a while, but the dialog must not hang forever.
    private static final long VEND_TIMEOUT_MS = 120_000;
    // How long the final message stays visible before the dialog closes.
    private static final long CLOSE_DELAY_MS = 2_000;

    private final List<RxBleDevice> mListRxBleDevices = new ArrayList<>();
    private final BleDeviceAdapter bleDeviceAdapter = new BleDeviceAdapter();
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private SwipeRefreshLayout mSwipeRefreshLayout;
    private View mProgressBar;
    private RxBleClient mRxBleClient;
    private Disposable mScanDisposable;

    // Application context: safe to use from background threads even after the fragment is detached
    // (requireContext()/getContext() there throw or return null when the user switches tabs).
    private Context mAppContext;

    @Override
    public void onAttach(@NonNull Context context) {
        super.onAttach(context);
        mAppContext = context.getApplicationContext();
    }

    // Run on the UI thread only while the fragment is still attached; avoids NPEs from getActivity()
    // / getContext() returning null when a background request finishes after leaving the tab.
    private void ui(Runnable r) {
        android.app.Activity activity = getActivity();
        if (activity != null) activity.runOnUiThread(() -> { if (isAdded()) r.run(); });
    }

    /** One BLE credit session. Only one may be open at a time (a second tap is ignored). */
    private final class BleSession {
        final RxBleDevice device;
        final AlertDialog dialog;
        volatile RxBleConnection connection;   // set once the GATT link is up
        volatile boolean ended;
        // Result of the last vend; SESSION COMPLETE arrives ~1 ms after it and must not hide it.
        volatile String outcome;
        Disposable disposable;
        final Runnable timeout = () -> cancel("Session timed out.");

        BleSession(RxBleDevice device, AlertDialog dialog) {
            this.device = device;
            this.dialog = dialog;
        }

        void message(String text) {
            ui(() -> { if (dialog.isShowing()) dialog.setMessage(text); });
        }

        void armTimeout(long ms) {
            mMainHandler.removeCallbacks(timeout);
            if (!ended) mMainHandler.postDelayed(timeout, ms);
        }

        /** Tell the machine to drop the session (best effort), then close the link. */
        void cancel(String reason) {
            if (ended) return;
            RxBleConnection c = connection;
            if (c == null) {
                end(reason);
                return;
            }
            message(reason);
            // A cancel write that fails or hangs must still release the link.
            Disposable ignored = c.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, new byte[]{CMD_CANCEL_SESSION})
                    .timeout(3, TimeUnit.SECONDS)
                    .subscribe((bytes, err) -> {
                        if (err != null) Log.w(TAG, "Cancel write failed", err);
                        end(reason);
                    });
        }

        /** Close the link, keep {@code reason} on screen briefly, then dismiss and rescan. */
        void end(@Nullable String reason) {
            if (ended) return;
            ended = true;
            mMainHandler.removeCallbacks(timeout);
            if (disposable != null) disposable.dispose();
            connection = null;
            if (reason != null) message(reason);
            mMainHandler.postDelayed(() -> {
                if (dialog.isShowing()) dialog.dismiss();
                if (mSession == this) mSession = null;
                ui(() -> {
                    mProgressBar.setVisibility(View.VISIBLE);
                    mListRxBleDevices.clear();
                    bleDeviceAdapter.notifyDataSetChanged();
                    fetchNearestData();
                });
            }, reason != null ? CLOSE_DELAY_MS : 0);
        }
    }

    private volatile BleSession mSession;

    /** Failure with a message that is safe to show to the user. */
    private static final class PaymentException extends Exception {
        PaymentException(String message) { super(message); }
    }

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

        @Override
        public BleDeviceViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_nearest_layout, parent, false);
            BleDeviceViewHolder holder = new BleDeviceViewHolder(view);

            view.setOnClickListener(v -> {
                int pos = holder.getAdapterPosition();
                if (pos == RecyclerView.NO_POSITION) return;
                if (mSession != null) {
                    // Double tap / second machine: one session at a time, the first one would leak.
                    Toast.makeText(getContext(), "A session is already open", Toast.LENGTH_SHORT).show();
                    return;
                }
                startSession(mListRxBleDevices.get(pos));
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
                    // Not trimmed: spaces at the ends are valid in a Wi-Fi password.
                    String password = editTextPassword.getText().toString();

                    if (ssid.isEmpty()) {
                        Toast.makeText(getContext(), "Select SSID", Toast.LENGTH_SHORT).show();
                        return;
                    }

                    // Fixed-size BLE frames: [cmd][text][\0]. Longer input overflowed the array and crashed.
                    if (ssid.getBytes(StandardCharsets.UTF_8).length > 20) {
                        Toast.makeText(getContext(), "SSID too long (max 20 characters)", Toast.LENGTH_LONG).show();
                        return;
                    }
                    if (password.getBytes(StandardCharsets.UTF_8).length > 61) {
                        Toast.makeText(getContext(), "Password too long (max 61 characters)", Toast.LENGTH_LONG).show();
                        return;
                    }

                    int pos = holder.getAdapterPosition();
                    if (pos == RecyclerView.NO_POSITION) return;

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

                    RxBleDevice rxBleDevice = mListRxBleDevices.get(pos);
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

    private void startSession(RxBleDevice device) {
        AlertDialog dialog = new AlertDialog.Builder(getContext())
                .setTitle("Send Credit (BLE)")
                .setMessage("Connecting...")
                .setCancelable(false)
                .setNegativeButton("Cancel", null)
                .create();

        BleSession s = new BleSession(device, dialog);
        mSession = s;
        dialog.show();
        // Override the default handler so the dialog stays up while the cancel is being delivered.
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setOnClickListener(b -> s.cancel("Cancelled."));
        s.armTimeout(IDLE_TIMEOUT_MS);

        s.disposable = device.establishConnection(false)
                // Retry only the connect phase (GATT 133 on quick reconnects). Once the session has
                // started, a retry would silently open a second session on the machine.
                .retry(2, t -> s.connection == null && !s.ended)
                .flatMap(connection -> connection.setupNotification(WRITE_CHARACTERISTIC_UUID)
                        .flatMap(notifications -> connection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, new byte[]{CMD_BEGIN_SESSION})
                                .toObservable()
                                .flatMap(ignored -> {
                                    Log.d(TAG, "Session started");
                                    s.connection = connection;
                                    s.message("Please select a product on the machine.");
                                    return notifications;
                                }))
                        // concatMap: handle one event at a time, in order.
                        .concatMap(notification -> onNotification(s, connection, notification)))
                .subscribe(
                        result -> Log.d(TAG, "Write completed"),
                        throwable -> {
                            Log.e(TAG, "BLE error", throwable);
                            s.end(s.connection == null ? "Could not connect to the machine." : "Connection to the machine was lost.");
                        },
                        () -> Log.d(TAG, "BLE stream complete"));
    }

    private Observable<byte[]> onNotification(BleSession s, RxBleConnection connection, byte[] notification) {
        if (notification.length == 0 || s.ended) return Observable.empty();
        Log.d(TAG, "Received: " + Integer.toHexString(notification[0]));

        switch (notification[0]) {
            case EVT_SESSION_COMPLETE:
                s.end(s.outcome != null ? s.outcome : "Session finished.");
                return Observable.empty();

            case EVT_VEND_FAILURE:
                s.armTimeout(IDLE_TIMEOUT_MS);
                s.outcome = "Purchase failed. Please try again.";
                s.message(s.outcome);
                return Observable.empty();

            case EVT_VEND_SUCCESS:
                s.armTimeout(IDLE_TIMEOUT_MS);
                s.outcome = "Product dispensed successfully!";
                s.message(s.outcome);
                return Observable.empty();

            case EVT_VEND_REQUEST:
                s.armTimeout(VEND_TIMEOUT_MS);
                s.outcome = null;
                s.message("Processing payment...");
                // Network on an IO thread: blocking here would stall the BLE callback thread.
                return Single.fromCallable(() -> requestCredit(s.device, notification))
                        .subscribeOn(Schedulers.io())
                        .flatMapObservable(payload -> connection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payload).toObservable())
                        .onErrorResumeNext(e -> {
                            String reason = e instanceof PaymentException ? e.getMessage() : "Payment failed.";
                            Log.w(TAG, "Payment refused: " + reason, e);
                            s.outcome = "Payment failed: " + reason;
                            s.message(s.outcome);
                            return connection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, new byte[]{CMD_CANCEL_SESSION}).toObservable();
                        });

            default:
                return Observable.empty();
        }
    }

    /** Calls request-credit and returns the signed approval payload for the machine. */
    private byte[] requestCredit(RxBleDevice device, byte[] notification) throws PaymentException {
        try {
            JSONObject body = new JSONObject();
            body.put("payload", Base64.encodeToString(notification, Base64.NO_WRAP));
            body.put("subdomain", device.getName().split("\\.")[0]);

            Location location = lastLocation();
            if (location != null) {
                body.put("lat", location.getLatitude());
                body.put("lng", location.getLongitude());
            }

            Request request = Api.request("/functions/v1/request-credit").post(Api.json(body)).build();
            try (Response response = Api.client(mAppContext).newCall(request).execute()) {
                String resBody = response.body().string();
                if (response.isSuccessful()) {
                    return Base64.decode(new JSONObject(resBody).getString("payload"), Base64.NO_WRAP);
                }
                if (Api.isSessionExpired(mAppContext, response)) {
                    Api.redirectToLogin(getActivity());
                    throw new PaymentException("session expired, please log in again");
                }
                throw new PaymentException(Api.errorMessage(response, resBody));
            }
        } catch (IOException e) {
            throw new PaymentException("no internet connection");
        } catch (JSONException | IllegalArgumentException e) {
            throw new PaymentException("invalid server response");
        }
    }

    /** Freshest fix from any enabled provider (GPS alone is usually null indoors). */
    @Nullable
    private Location lastLocation() {
        try {
            LocationManager lm = (LocationManager) mAppContext.getSystemService(Context.LOCATION_SERVICE);
            if (lm == null) return null;
            Location best = null;
            for (String provider : lm.getProviders(true)) {
                Location l = lm.getLastKnownLocation(provider);
                if (l != null && (best == null || l.getTime() > best.getTime())) best = l;
            }
            return best;
        } catch (SecurityException e) {
            return null; // location permission not granted: send without coordinates
        }
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        mProgressBar = view.findViewById(R.id.progressBar);
        mRxBleClient = RxBleClient.create(mAppContext);

        RecyclerView recyclerView = view.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter(bleDeviceAdapter);

        mSwipeRefreshLayout = view.findViewById(R.id.swipeRefresh);
        mSwipeRefreshLayout.setOnRefreshListener(this::fetchNearestData);
    }

    private void fetchNearestData() {
        if (mRxBleClient == null) return;
        if (mScanDisposable != null) mScanDisposable.dispose();

        // Collect off-thread, swap on the UI thread (background list mutation crashes RecyclerView).
        List<RxBleDevice> found = new ArrayList<>();
        mScanDisposable = mRxBleClient.scanBleDevices(new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build())
                .take(3, TimeUnit.SECONDS)
                .filter(scanResult -> scanResult.getBleDevice().getName() != null
                        && !scanResult.getBleDevice().getName().isEmpty()
                        && scanResult.getBleDevice().getName().endsWith(".vmflow.xyz")
                        && !scanResult.getBleDevice().getName().equals("0.vmflow.xyz"))
                .distinct(scanResult -> scanResult.getBleDevice().getMacAddress())
                .subscribe(scanResult -> {
                    Log.d(TAG, scanResult.getBleDevice().getName());
                    synchronized (found) { found.add(scanResult.getBleDevice()); }
                }, error -> {
                    Log.e(TAG, "BLE scan error", error);
                    // e.g. Bluetooth off / permission missing: don't leave the spinner running forever.
                    ui(() -> {
                        mSwipeRefreshLayout.setRefreshing(false);
                        mProgressBar.setVisibility(View.GONE);
                        Toast.makeText(getContext(), "Bluetooth scan failed. Check that Bluetooth is on.", Toast.LENGTH_LONG).show();
                    });
                }, () -> ui(() -> {
                    mListRxBleDevices.clear();
                    synchronized (found) { mListRxBleDevices.addAll(found); }
                    bleDeviceAdapter.notifyDataSetChanged();
                    mSwipeRefreshLayout.setRefreshing(false);
                    mProgressBar.setVisibility(View.GONE);

                    if (mListRxBleDevices.isEmpty())
                        Toast.makeText(getContext(), "No devices found nearby", Toast.LENGTH_LONG).show();

                    Log.d(TAG, "Scan complete");
                }));
    }

    @Override
    public void onStart() {
        super.onStart();

        mProgressBar.setVisibility(View.VISIBLE);
        fetchNearestData();
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        if (mScanDisposable != null) mScanDisposable.dispose();
        BleSession s = mSession;
        if (s != null) {
            // Leaving the tab must not strand an open session on the machine: send CANCEL first.
            if (s.dialog.isShowing()) s.dialog.dismiss();
            s.cancel(null);
            mSession = null;
        }
        if (bleWifiDisposable != null && !bleWifiDisposable.isDisposed()) {
            bleWifiDisposable.dispose();
        }
    }

    private Disposable bleWifiDisposable;

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_nearest, container, false);
    }
}
