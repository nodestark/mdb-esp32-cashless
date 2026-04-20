package xyz.vmflow.target;

import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
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
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import xyz.vmflow.BuildConfig;
import xyz.vmflow.R;

public class EmbeddesFragment extends Fragment {
    private static final String TAG = "EmbeddesFragment";
    private static final OkHttpClient HTTP_CLIENT_EMB = new OkHttpClient();

    private static final UUID WRITE_CHARACTERISTIC_UUID = UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002");

    private final List<JSONObject> mListEmbeddeds = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    private RxBleClient mRxBleClient;
    private View mProgressBar;

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
                if (pos != RecyclerView.NO_ID)
                    showLinkMachineDialog(mListEmbeddeds.get(pos), pos);
            });

            holder.btnSendCredit.setOnClickListener(v -> {

                AlertDialog.Builder builder = new AlertDialog.Builder(getContext());

                View dialogView = LayoutInflater.from(getContext()).inflate(R.layout.dialog_credit_settings, null);
                builder.setView(dialogView);

                builder.setTitle("Send Credit (MQTT)");
                builder.setNegativeButton("Cancel", (dialog, which) -> dialog.dismiss());
                builder.setPositiveButton("Send", (dialog, which) -> {

                    ProgressDialog progressDialog = new ProgressDialog(getContext());
                    progressDialog.setMessage("Loading...");
                    progressDialog.setCancelable(false);
                    progressDialog.show();

                    ExecutorService executor = Executors.newSingleThreadExecutor();
                    executor.execute(() -> {

                        AutoCompleteTextView editTextAmount = dialogView.findViewById(R.id.editTextAmount);

                        Float amount = Float.valueOf( editTextAmount.getText().toString() );

                        boolean retry;
                        do {
                            retry = false;

                            try {
                                SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);

                                JSONObject jsonEmbedded = mListEmbeddeds.get(holder.getAdapterPosition());

                                JSONObject jsonObjectSend= new JSONObject();
                                jsonObjectSend.put("amount", amount.floatValue() );
                                jsonObjectSend.put("subdomain", jsonEmbedded.getInt("subdomain") );

                                RequestBody requestBody = RequestBody.create( jsonObjectSend.toString(), MediaType.parse("application/json") );

                                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                                Request request = new Request.Builder().url(BuildConfig.SUPABASE_URL + "/functions/v1/send-credit")
                                        .post(requestBody)
                                        .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                                        .addHeader("Content-Type", "application/json")
                                        .build();

                                Response response = new OkHttpClient().newCall(request).execute();
                                if(response.isSuccessful()){

                                    Log.d(TAG, response.body().string());
                                    getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Success", Toast.LENGTH_SHORT).show());

                                } else if (response.code() == 401) {

                                    RequestBody requestBody_ = RequestBody.create( jsonAuth.toString(), MediaType.parse("application/json") );

                                    Request request_ = new Request.Builder().url(BuildConfig.SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                                            .post(requestBody_)
                                            .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                                            .addHeader("Content-Type", "application/json")
                                            .build();

                                    Response response_ = new OkHttpClient().newCall(request_).execute();
                                    if (response_.isSuccessful()) {

                                        SharedPreferences.Editor editor = prefs.edit();
                                        editor.putString("auth_json", response_.body().string());
                                        editor.apply();

                                        retry = true;
                                    } else {
                                        redirectToLogin();
                                    }
                                } else {
                                    getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Error: " + response.code(), Toast.LENGTH_SHORT).show());
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

            return holder;
        }

        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

            JSONObject jsonEmbedded = mListEmbeddeds.get(position);

            try {
                holder.deviceNameText.setText(String.format("Device: %06d", jsonEmbedded.getInt("subdomain")));

                int color = ContextCompat.getColor(getContext(), "online".equals(jsonEmbedded.getString("status")) ? R.color.green : R.color.red);
                holder.viewDeviceOffline.setBackgroundColor(color);

                JSONObject machine = jsonEmbedded.optJSONObject("machine");
                holder.tvMachineName.setText(machine != null ? machine.optString("name", "—") : "No machine");
            } catch (JSONException e) {
                throw new RuntimeException(e);
            }
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
                            getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Embedded not found", Toast.LENGTH_SHORT).show());
                        }
                    });
        });

        SwipeRefreshLayout swipeRefreshLayout = view_.findViewById(R.id.swipeRefresh);
        swipeRefreshLayout.setOnRefreshListener(() -> {

            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {

                fetchEmbeddedsData();

                getActivity().runOnUiThread(() -> swipeRefreshLayout.setRefreshing(false));
            });
        });
    }

    private void fetchEmbeddedsData() {

        boolean retry;
        do {
            retry = false; // controls whether the request needs to be retried

            try {
                SharedPreferences prefs = getContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);

                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                Request request = new Request.Builder()
                        .url(BuildConfig.SUPABASE_URL + "/rest/v1/embedded?select=id,subdomain,status,machine_id,machine:machines(id,name)&order=subdomain")
                        .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                        .addHeader("Content-Type", "application/json")
                        .build();

                Response response = HTTP_CLIENT_EMB.newCall(request).execute();
                if(response.isSuccessful()){

                    try {
                        JSONArray jsonArray = new JSONArray(response.body().string());

                        mListEmbeddeds.clear();
                        for (int i = 0; i < jsonArray.length(); i++) {
                            mListEmbeddeds.add(jsonArray.getJSONObject(i));
                        }

                        if(getActivity() != null)
                            getActivity().runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());

                    } catch (JSONException e) {
                        e.printStackTrace();
                    }

                } else if (response.code() == 401) {

                    RequestBody requestBody = RequestBody.create( jsonAuth.toString(), MediaType.parse("application/json") );

                    Request request_ = new Request.Builder().url(BuildConfig.SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                            .post(requestBody)
                            .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                            .addHeader("Content-Type", "application/json")
                            .build();

                    Response response_ = new OkHttpClient().newCall(request_).execute();
                    if (response_.isSuccessful()) {

                        SharedPreferences.Editor editor = prefs.edit();
                        editor.putString("auth_json", response_.body().string());
                        editor.apply();

                        retry = true;
                    } else {
                        redirectToLogin();
                    }
                } else if(getActivity() != null)
                    getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Error: " + response.code(), Toast.LENGTH_SHORT).show());

            } catch (JSONException | IOException e) {
                e.printStackTrace();
            }

        } while(retry);

        if(getActivity() != null)
            getActivity().runOnUiThread(() -> mProgressBar.setVisibility(View.GONE) );
    }

    private void rxBleInstall(ScanResult scanResult) {
        Log.d(TAG, "BLE install started");

        RxBleDevice rxBleDevice = scanResult.getBleDevice();

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {

            boolean retry;
            do {
                retry = false;

                try {
                    SharedPreferences prefs = getContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);

                    JSONObject jsonObject= new JSONObject();
                    jsonObject.put("mac_address", rxBleDevice.getMacAddress());

                    RequestBody requestBody = RequestBody.create( jsonObject.toString(), MediaType.parse("application/json") );

                    JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                    Request request = new Request.Builder().url(BuildConfig.SUPABASE_URL + "/rest/v1/embedded")
                            .post(requestBody)
                            .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                            .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                            .addHeader("Content-Type", "application/json")
                            .addHeader("Prefer", "return=representation")
                            .build();

                    Response response = new OkHttpClient().newCall(request).execute();
                    if(response.isSuccessful()){

                        JSONArray jsonArray = new JSONArray(response.body().string());
                        JSONObject jsonNewEmbedded = jsonArray.getJSONObject(0);

                        Disposable disposable = rxBleDevice.establishConnection(false)
                                .flatMap(rxBleConnection -> {
                                    Log.d(TAG, "BLE connected");

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
                                                Log.d(TAG, "Write 1 OK: " + new String(bytes));
                                                return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadPasskey);
                                            }).toObservable();
                                })
                                .subscribe(
                                        bytes -> {
                                            Log.d(TAG, "Write 2 OK: " + new String(bytes));
                                        },
                                        err -> {
                                            Log.e(TAG, "BLE connection or write error", err);

                                            getActivity().runOnUiThread(() ->
                                                    Toast.makeText(getContext(), "BLE error", Toast.LENGTH_SHORT).show()
                                            );
                                        }, () -> {}
                                );

                        mListEmbeddeds.add(jsonNewEmbedded);

                        AlertDialog.Builder builder = new AlertDialog.Builder(getContext());
                        builder.setTitle("Registered!");
                        builder.setMessage(jsonNewEmbedded.getString("subdomain"));
                        builder.setPositiveButton("Ok", null );

                        getActivity().runOnUiThread(() -> {
                            itemAdapter_.notifyDataSetChanged();
                            builder.create().show();
                        } );

                    } else if (response.code() == 401) {

                        RequestBody requestBody_ = RequestBody.create( jsonAuth.toString(), MediaType.parse("application/json") );

                        Request request_ = new Request.Builder().url(BuildConfig.SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                                .post(requestBody_)
                                .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                                .addHeader("Content-Type", "application/json")
                                .build();

                        Response response_ = new OkHttpClient().newCall(request_).execute();
                        if (response_.isSuccessful()) {

                            SharedPreferences.Editor editor = prefs.edit();
                            editor.putString("auth_json", response_.body().string());
                            editor.apply();

                            retry = true;
                        } else {
                            redirectToLogin();
                        }
                    } else {
                        getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Error: " + response.code(), Toast.LENGTH_SHORT).show());
                    }

                } catch (JSONException | IOException e) {
                    e.printStackTrace();
                }
            } while(retry);
        });
    }

    private void showLinkMachineDialog(JSONObject embedded, int embeddedPos) {
        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            try {
                SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);
                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                Request request = new Request.Builder()
                        .url(BuildConfig.SUPABASE_URL + "/rest/v1/machines?select=id,name&order=name")
                        .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                        .build();

                Response response = HTTP_CLIENT_EMB.newCall(request).execute();
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
                getActivity().runOnUiThread(() -> {
                    int[] picked = {selectedIndex[0]};
                    new AlertDialog.Builder(getContext())
                            .setTitle("Link machine")
                            .setSingleChoiceItems(labels, selectedIndex[0], (d, which) -> picked[0] = which)
                            .setPositiveButton("Save", (d, which) -> {
                                ExecutorService exec2 = Executors.newSingleThreadExecutor();
                                exec2.execute(() -> {
                                    try {
                                        SharedPreferences p2 = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);
                                        JSONObject auth2 = new JSONObject(p2.getString("auth_json", "{}"));

                                        JSONObject body = new JSONObject();
                                        if (picked[0] == 0) {
                                            body.put("machine_id", JSONObject.NULL);
                                        } else {
                                            body.put("machine_id", finalMachines.getJSONObject(picked[0] - 1).getString("id"));
                                        }

                                        Request req = new Request.Builder()
                                                .url(BuildConfig.SUPABASE_URL + "/rest/v1/embedded?id=eq." + embedded.getString("id"))
                                                .patch(RequestBody.create(body.toString(), MediaType.parse("application/json")))
                                                .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                                                .addHeader("Authorization", "Bearer " + auth2.getString("access_token"))
                                                .addHeader("Content-Type", "application/json")
                                                .build();

                                        Response res = HTTP_CLIENT_EMB.newCall(req).execute();
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
                                            if (getActivity() != null)
                                                getActivity().runOnUiThread(() -> {
                                                    itemAdapter_.notifyItemChanged(embeddedPos);
                                                    Toast.makeText(getContext(), "Vinculado", Toast.LENGTH_SHORT).show();
                                                });
                                        } else if (getActivity() != null) {
                                            getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Erro: " + res.code(), Toast.LENGTH_SHORT).show());
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