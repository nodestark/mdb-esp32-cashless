package xyz.vmflow.target;

import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AutoCompleteTextView;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.google.android.material.floatingactionbutton.FloatingActionButton;
import com.polidea.rxandroidble3.RxBleClient;
import com.polidea.rxandroidble3.RxBleDevice;
import com.polidea.rxandroidble3.scan.ScanResult;

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
import okhttp3.Request;
import okhttp3.Response;
import xyz.vmflow.R;

public class EmbeddesFragment extends Fragment {
    private static final String TAG = "EmbeddesFragment";

    private static final UUID WRITE_CHARACTERISTIC_UUID = UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002");

    private final List<JSONObject> mListEmbeddeds = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    private RxBleClient mRxBleClient;
    private View mProgressBar;

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

    class ViewHolder_ extends RecyclerView.ViewHolder {
        View viewDeviceOffline;
        TextView deviceNameText;
        TextView tvMachineName;
        ImageButton btnSendCredit;
        ImageButton btnLinkMachine;

        public ViewHolder_(@NonNull View itemView) {
            super(itemView);

            deviceNameText = itemView.findViewById(R.id.textViewDeviceAlias);
            tvMachineName = itemView.findViewById(R.id.textViewMachineName);
            btnSendCredit = itemView.findViewById(R.id.btnSendCredit);
            btnLinkMachine = itemView.findViewById(R.id.btnLinkMachine);
            viewDeviceOffline = itemView.findViewById(R.id.viewDeviceOffline);
        }
    }

    class ItemAdapter_ extends RecyclerView.Adapter<ViewHolder_> {

        public ViewHolder_ onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_embedded_layout, parent, false);
            ViewHolder_ holder = new ViewHolder_(view);

            holder.btnLinkMachine.setOnClickListener(v -> {
                int pos = holder.getAdapterPosition();
                if (pos != RecyclerView.NO_POSITION)
                    showLinkMachineDialog(mListEmbeddeds.get(pos), pos);
            });

            holder.btnSendCredit.setOnClickListener(v -> {

                AlertDialog.Builder builder = new AlertDialog.Builder(getContext());

                View dialogView = LayoutInflater.from(getContext()).inflate(R.layout.dialog_credit_settings, null);
                builder.setView(dialogView);

                builder.setTitle("Send Credit (MQTT)");
                builder.setNegativeButton("Cancel", (dialog, which) -> dialog.dismiss());
                builder.setPositiveButton("Send", (dialog, which) -> {

                    // Validate on the UI thread: an empty or "1,50" amount used to throw
                    // NumberFormatException on the worker thread, which crashed the app.
                    AutoCompleteTextView editTextAmount = dialogView.findViewById(R.id.editTextAmount);
                    float amount;
                    try {
                        amount = Float.parseFloat(editTextAmount.getText().toString().trim().replace(',', '.'));
                    } catch (NumberFormatException e) {
                        amount = -1;
                    }
                    if (amount <= 0) {
                        Toast.makeText(getContext(), "Invalid amount", Toast.LENGTH_SHORT).show();
                        return;
                    }
                    final float creditAmount = amount;

                    int pos = holder.getAdapterPosition();
                    if (pos == RecyclerView.NO_POSITION) return;
                    JSONObject jsonEmbedded = mListEmbeddeds.get(pos);

                    ProgressDialog progressDialog = new ProgressDialog(getContext());
                    progressDialog.setMessage("Loading...");
                    progressDialog.setCancelable(false);
                    progressDialog.show();

                    ExecutorService executor = Executors.newSingleThreadExecutor();
                    executor.execute(() -> {
                        try {
                            JSONObject jsonObjectSend = new JSONObject();
                            jsonObjectSend.put("amount", creditAmount);
                            jsonObjectSend.put("subdomain", jsonEmbedded.getInt("subdomain"));

                            Request request = Api.request("/functions/v1/send-credit").post(Api.json(jsonObjectSend)).build();
                            try (Response response = Api.client(mAppContext).newCall(request).execute()) {
                                String body = response.body().string();
                                if (response.isSuccessful()) {
                                    Log.d(TAG, body);
                                    ui(() -> Toast.makeText(getContext(), "Success", Toast.LENGTH_SHORT).show());
                                } else if (Api.isSessionExpired(mAppContext, response)) {
                                    Api.redirectToLogin(getActivity());
                                } else {
                                    String reason = Api.errorMessage(response, body);
                                    ui(() -> Toast.makeText(getContext(), "Error: " + reason, Toast.LENGTH_LONG).show());
                                }
                            }
                        } catch (JSONException | IOException e) {
                            Log.e(TAG, "send-credit failed", e);
                            ui(() -> Toast.makeText(getContext(), "Network error", Toast.LENGTH_SHORT).show());
                        } finally {
                            ui(progressDialog::dismiss);
                        }
                    });
                    executor.shutdown();
                });

                AlertDialog dialog = builder.create();
                dialog.show();
            });

            return holder;
        }

        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

            JSONObject jsonEmbedded = mListEmbeddeds.get(position);

            // opt* instead of get*: a row missing status/subdomain (e.g. just registered) used to throw and crash.
            holder.deviceNameText.setText(String.format("Device: %06d", jsonEmbedded.optInt("subdomain")));

            int color = ContextCompat.getColor(holder.itemView.getContext(), "online".equals(jsonEmbedded.optString("status")) ? R.color.green : R.color.red);
            holder.viewDeviceOffline.setBackgroundColor(color);

            JSONObject machine = jsonEmbedded.optJSONObject("machine");
            holder.tvMachineName.setText(machine != null ? machine.optString("name", "—") : "No machine");
        }

        @Override
        public int getItemCount() {
            return mListEmbeddeds.size();
        }
    }

    @Override
    public void onViewCreated(@NonNull View view_, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view_, savedInstanceState);

        mProgressBar = view_.findViewById(R.id.progressBar);

        mRxBleClient = RxBleClient.create(getContext());

        RecyclerView recyclerView = view_.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter( itemAdapter_ );

        FloatingActionButton fab = view_.findViewById(R.id.fabAdd);

        fab.setOnClickListener(view -> {

            ProgressDialog progressDialog = new ProgressDialog(getContext());

            progressDialog.setMessage("Connecting to 0.vmflow.xyz");
            progressDialog.setCancelable(false);

            progressDialog.show();

            Disposable disposable = mRxBleClient.scanBleDevices(new com.polidea.rxandroidble3.scan.ScanSettings.Builder().build(), new com.polidea.rxandroidble3.scan.ScanFilter.Builder().setDeviceName("0.vmflow.xyz").build())
                    .firstElement()
                    .timeout(3, TimeUnit.SECONDS)
                    .doFinally(() -> progressDialog.dismiss())
                    .subscribe( EmbeddesFragment.this::rxBleInstall, th -> {

                        if (th instanceof TimeoutException) {
                            ui(() -> Toast.makeText(getContext(), "Embedded not found", Toast.LENGTH_SHORT).show());
                        }
                    });
        });

        SwipeRefreshLayout swipeRefreshLayout = view_.findViewById(R.id.swipeRefresh);
        swipeRefreshLayout.setOnRefreshListener(() -> {

            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {

                fetchEmbeddedsData();

                ui(() -> swipeRefreshLayout.setRefreshing(false));
            });
        });
    }

    private void fetchEmbeddedsData() {
        try {
            Request request = Api.request("/rest/v1/embedded?select=id,subdomain,status,machine_id,machine:machines(id,name)&order=subdomain").build();
            try (Response response = Api.client(mAppContext).newCall(request).execute()) {
                String body = response.body().string();
                if (response.isSuccessful()) {
                    JSONArray jsonArray = new JSONArray(body);

                    // Build off-thread, swap on the UI thread (background list mutation crashes RecyclerView).
                    List<JSONObject> fresh = new ArrayList<>();
                    for (int i = 0; i < jsonArray.length(); i++) {
                        fresh.add(jsonArray.getJSONObject(i));
                    }

                    ui(() -> {
                        mListEmbeddeds.clear();
                        mListEmbeddeds.addAll(fresh);
                        itemAdapter_.notifyDataSetChanged();
                    });
                } else if (Api.isSessionExpired(mAppContext, response)) {
                    Api.redirectToLogin(getActivity());
                } else {
                    String reason = Api.errorMessage(response, body);
                    ui(() -> Toast.makeText(getContext(), "Error: " + reason, Toast.LENGTH_SHORT).show());
                }
            }
        } catch (JSONException | IOException e) {
            Log.e(TAG, "fetch embedded failed", e);
        }

        ui(() -> mProgressBar.setVisibility(View.GONE) );
    }

    private void rxBleInstall(ScanResult scanResult) {
        Log.d(TAG, "BLE install started");

        RxBleDevice rxBleDevice = scanResult.getBleDevice();

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            try {
                JSONObject jsonObject = new JSONObject();
                jsonObject.put("mac_address_", rxBleDevice.getMacAddress());

                // register_embedded is idempotent per MAC: retrying after a failed BLE write returns
                // the same subdomain/passkey instead of creating a second device.
                Request request = Api.request("/rest/v1/rpc/register_embedded").post(Api.json(jsonObject)).build();
                JSONObject jsonNewEmbedded;
                try (Response response = Api.client(mAppContext).newCall(request).execute()) {
                    String body = response.body().string();
                    if (Api.isSessionExpired(mAppContext, response)) {
                        Api.redirectToLogin(getActivity());
                        return;
                    }
                    if (!response.isSuccessful()) {
                        String reason = Api.errorMessage(response, body);
                        ui(() -> Toast.makeText(getContext(), "Error: " + reason, Toast.LENGTH_LONG).show());
                        return;
                    }
                    jsonNewEmbedded = new JSONArray(body).getJSONObject(0);
                }

                byte[] payloadSubdomain = framed((byte) 0x00, jsonNewEmbedded.getString("subdomain"));
                byte[] payloadPasskey = framed((byte) 0x01, jsonNewEmbedded.getString("passkey"));
                String newSubdomain = jsonNewEmbedded.optString("subdomain");

                ui(() -> mProgressBar.setVisibility(View.VISIBLE));

                // Only report success once the board has actually stored both values.
                Disposable ignored = rxBleDevice.establishConnection(false)
                        .retry(2) // safe here: both writes are idempotent (firmware keeps the first value)
                        .flatMapSingle(rxBleConnection -> rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadSubdomain)
                                .timeout(5, TimeUnit.SECONDS)
                                .flatMap(bytes -> rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadPasskey)
                                        .timeout(5, TimeUnit.SECONDS)))
                        .firstOrError()
                        .timeout(30, TimeUnit.SECONDS)
                        .subscribe(
                                bytes -> {
                                    Log.d(TAG, "Provisioning written");
                                    ui(() -> {
                                        mProgressBar.setVisibility(View.GONE);
                                        boolean known = false;
                                        for (JSONObject e : mListEmbeddeds)
                                            if (e.optString("id").equals(jsonNewEmbedded.optString("id"))) known = true;
                                        if (!known) {
                                            mListEmbeddeds.add(jsonNewEmbedded);
                                            itemAdapter_.notifyItemInserted(mListEmbeddeds.size() - 1);
                                        }
                                        new AlertDialog.Builder(getContext())
                                                .setTitle("Registered!")
                                                .setMessage(newSubdomain)
                                                .setPositiveButton("Ok", null)
                                                .show();
                                    });
                                },
                                err -> {
                                    Log.e(TAG, "BLE connection or write error", err);
                                    ui(() -> {
                                        mProgressBar.setVisibility(View.GONE);
                                        new AlertDialog.Builder(getContext())
                                                .setTitle("Device not configured")
                                                .setMessage("Device " + newSubdomain + " was registered, but writing its settings over Bluetooth failed. Keep the board close to the phone and tap + again.")
                                                .setPositiveButton("Ok", null)
                                                .show();
                                    });
                                });

            } catch (JSONException | IOException e) {
                Log.e(TAG, "register_embedded failed", e);
                ui(() -> Toast.makeText(getContext(), "Network error", Toast.LENGTH_SHORT).show());
            }
        });
        executor.shutdown();
    }

    // Fixed-size BLE frame: [cmd][utf-8 text][\0], 22 bytes (firmware strcpy's it).
    private static byte[] framed(byte cmd, String text) {
        byte[] arr = text.getBytes(StandardCharsets.UTF_8);
        if (arr.length > 20) throw new IllegalArgumentException("BLE field too long");
        byte[] out = new byte[22];
        out[0] = cmd;
        System.arraycopy(arr, 0, out, 1, arr.length);
        return out;
    }

    private void showLinkMachineDialog(JSONObject embedded, int embeddedPos) {
        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            try {
                Request request = Api.request("/rest/v1/machines?select=id,name&order=name").build();

                Response response = Api.client(mAppContext).newCall(request).execute();
                if (!response.isSuccessful() || getActivity() == null) return;

                JSONArray machines = new JSONArray(response.body().string());

                // Build labels: "Nenhuma" + machine names
                String[] labels = new String[machines.length() + 1];
                labels[0] = "Nenhuma";
                for (int i = 0; i < machines.length(); i++)
                    labels[i + 1] = machines.getJSONObject(i).optString("name", "—");

                // Find current selection
                String currentMachineId = embedded.optString("machine_id", "");
                int[] selectedIndex = {0};
                for (int i = 0; i < machines.length(); i++) {
                    if (machines.getJSONObject(i).optString("id", "").equals(currentMachineId)) {
                        selectedIndex[0] = i + 1;
                        break;
                    }
                }

                JSONArray finalMachines = machines;
                ui(() -> {
                    int[] picked = {selectedIndex[0]};
                    new AlertDialog.Builder(getContext())
                            .setTitle("Link machine")
                            .setSingleChoiceItems(labels, selectedIndex[0], (d, which) -> picked[0] = which)
                            .setPositiveButton("Save", (d, which) -> {
                                ExecutorService exec2 = Executors.newSingleThreadExecutor();
                                exec2.execute(() -> {
                                    try {
                                        JSONObject body = new JSONObject();
                                        if (picked[0] == 0) {
                                            body.put("machine_id", JSONObject.NULL);
                                        } else {
                                            body.put("machine_id", finalMachines.getJSONObject(picked[0] - 1).getString("id"));
                                        }

                                        Request req = Api.request("/rest/v1/embedded?id=eq." + embedded.getString("id"))
                                                .patch(Api.json(body))
                                                .build();

                                        Response res = Api.client(mAppContext).newCall(req).execute();
                                        if (res.isSuccessful()) {
                                            // Update local object
                                            if (picked[0] == 0) {
                                                embedded.put("machine_id", JSONObject.NULL);
                                                embedded.put("machine", JSONObject.NULL);
                                            } else {
                                                JSONObject m = finalMachines.getJSONObject(picked[0] - 1);
                                                embedded.put("machine_id", m.getString("id"));
                                                embedded.put("machine", m);
                                            }
                                            ui(() -> {
                                                    itemAdapter_.notifyItemChanged(embeddedPos);
                                                    Toast.makeText(getContext(), "Vinculado", Toast.LENGTH_SHORT).show();
                                                });
                                        } else if (getActivity() != null) {
                                            ui(() -> Toast.makeText(getContext(), "Erro: " + res.code(), Toast.LENGTH_SHORT).show());
                                        }
                                    } catch (JSONException | IOException e) {
                                        e.printStackTrace();
                                    }
                                });
                            })
                            .setNegativeButton("Cancelar", null)
                            .show();
                });

            } catch (JSONException | IOException e) {
                e.printStackTrace();
            }
        });
    }

    @Override
    public void onStart() {
        super.onStart();

        mProgressBar.setVisibility(View.VISIBLE);

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            fetchEmbeddedsData();
        });
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_embeddes, container, false);
    }
}