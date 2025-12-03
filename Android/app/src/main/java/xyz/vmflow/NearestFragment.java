package xyz.vmflow;

import android.app.AlertDialog;
import android.content.Context;
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

public class NearestFragment extends Fragment {

    private static final String SUPABASE_URL = "https://supabase.vmflow.xyz";
    private static final String SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4";

    private static final UUID WRITE_CHARACTERISTIC_UUID = UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002");

    private final List<RxBleDevice> mListRxBleDevices = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();
    private SwipeRefreshLayout mSwipeRefreshLayout;
    private View mProgressBar;

    private Disposable bleConnectionDisposable;

    class ViewHolder_ extends RecyclerView.ViewHolder {

        TextView viewText;
        ImageButton btnSetPassword;

        public ViewHolder_(@NonNull View itemView) {
            super(itemView);

            viewText = itemView.findViewById(R.id.textViewNearest);
            btnSetPassword = itemView.findViewById(R.id.btnSetPassword);
        }
    }

    class ItemAdapter_ extends RecyclerView.Adapter<ViewHolder_> {

        private RxBleConnection mRxBleConnection;

        public ViewHolder_ onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_nearest_layout, parent, false);
            ViewHolder_ holder = new ViewHolder_(view);

            view.setOnClickListener(v -> {
                int pos = holder.getAdapterPosition();
                if (pos != RecyclerView.NO_POSITION) {

                    RxBleDevice device = mListRxBleDevices.get(pos);

                    AlertDialog.Builder builder = new AlertDialog.Builder(getContext());
                    builder.setNegativeButton("Cancel", (dialog_, which) -> {

                        if(mRxBleConnection != null){
                            Disposable dis = mRxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, new byte[]{0x04} /*cancel_session*/).toObservable().subscribe(bytes -> {
                            }, throwable -> {
                            }, () -> {
                                bleConnectionDisposable.dispose();

                                getActivity().runOnUiThread(() -> {

                                    mProgressBar.setVisibility(View.VISIBLE);

                                    mListRxBleDevices.clear();
                                    itemAdapter_.notifyDataSetChanged();
                                } );

                                ExecutorService executor = Executors.newSingleThreadExecutor();
                                executor.execute(() -> {
                                    fetchNearestData();
                                });
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
                            .flatMap(rxBleConnection -> rxBleConnection.writeCharacteristic( WRITE_CHARACTERISTIC_UUID, new byte[]{0x02 /*begin_session*/} ).toObservable()
                                    .flatMap(initResult -> {
                                        Log.d("rxBle_", "Sessão iniciada: ");

                                        getActivity().runOnUiThread(() -> dialog.setMessage("Please select a product on the machine.") );

                                        mRxBleConnection = rxBleConnection;
                                        
                                        return rxBleConnection.setupNotification(WRITE_CHARACTERISTIC_UUID)
                                                .flatMap(notificationObservable ->
                                                        notificationObservable.flatMap(bytes -> {

                                                            Log.d("rxBle_", "Recebido: " + Integer.toHexString(bytes[0]));

                                                            if(bytes[0] == 0x0d /*session_complete*/){
                                                                bleConnectionDisposable.dispose();

                                                                getActivity().runOnUiThread(() -> {

                                                                    dialog.setMessage("Session finished.");

                                                                    mProgressBar.setVisibility(View.VISIBLE);

                                                                    mListRxBleDevices.clear();
                                                                    itemAdapter_.notifyDataSetChanged();
                                                                } );

                                                                ExecutorService executor = Executors.newSingleThreadExecutor();
                                                                executor.execute(() -> {
                                                                    fetchNearestData();
                                                                });
                                                            }

                                                            if(bytes[0] == 0x0c /*vend_failure*/){
                                                                getActivity().runOnUiThread(() -> dialog.setMessage("Purchase failed. Please try again.") );
                                                            }

                                                            if(bytes[0] == 0x0b /*vend_succecss*/){
                                                                getActivity().runOnUiThread(() -> dialog.setMessage("Product dispensed successfully!") );
                                                            }

                                                            if(bytes[0] == 0x0a /*vend_request*/){
                                                                getActivity().runOnUiThread(() -> dialog.setMessage("Processing payment...") );

                                                                boolean retry;
                                                                do {
                                                                    retry = false;

                                                                    try {

                                                                        LocationManager locationManager = (LocationManager) getContext().getSystemService(Context.LOCATION_SERVICE);
                                                                        Location location = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);

                                                                        SharedPreferences prefs = getContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);

                                                                        JSONObject jsonObjectSend= new JSONObject();
                                                                        jsonObjectSend.put("payload", Base64.encodeToString(bytes, Base64.NO_WRAP));
                                                                        jsonObjectSend.put("subdomain", device.getName().split("\\.")[0] );
                                                                        jsonObjectSend.put("lat", location.getLatitude());
                                                                        jsonObjectSend.put("lng", location.getLongitude() );

                                                                        RequestBody requestBody = RequestBody.create( jsonObjectSend.toString(), MediaType.parse("application/json") );

                                                                        JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                                                                        Request request = new Request.Builder().url(SUPABASE_URL + "/functions/v1/request-credit")
                                                                                .post(requestBody)
                                                                                .addHeader("apikey", SUPABASE_KEY)
                                                                                .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                                                                                .addHeader("Content-Type", "application/json")
                                                                                .build();

                                                                        Response response = new OkHttpClient().newCall(request).execute();
                                                                        if(response.isSuccessful()){

                                                                            JSONObject jsonObject = new JSONObject(response.body().string());
                                                                            byte[] payload = Base64.decode(jsonObject.getString("payload"), Base64.NO_WRAP);

                                                                            return rxBleConnection.writeCharacteristic( WRITE_CHARACTERISTIC_UUID, payload ).toObservable();

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
                                                                        }

                                                                    } catch (JSONException | IOException e) {
                                                                        e.printStackTrace();
                                                                    }

                                                                } while(retry);

                                                                getActivity().runOnUiThread(() -> dialog.setMessage("Falha no pagamento.") );

                                                                return rxBleConnection.writeCharacteristic( WRITE_CHARACTERISTIC_UUID, new byte[]{0x04 /*cancel_session*/} ).toObservable();
                                                            }
                                                            return Observable.empty();
                                                        })
                                                );
                                            })
                            )
                            .retry(2)
                            .subscribe(
                                    result -> Log.d("rxBle_", "Escrita/Resposta concluída"),
                                    throwable -> Log.e("rxBle_", "Erro BLE", throwable),
                                    () -> {
                                        Log.d("rxBle_", "It's all");
                                    }
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

                    if (ssid.isEmpty() ) {
                        Toast.makeText(getContext(), "Select SSID", Toast.LENGTH_SHORT).show();
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

                    RxBleDevice rxBleDevice = mListRxBleDevices.get(holder.getAdapterPosition());
                    Disposable disposable = rxBleDevice.establishConnection(false)
                            .flatMap(rxBleConnection -> {
                                return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadSsid)
                                        .timeout(3, TimeUnit.SECONDS)
                                        .flatMap(bytes -> {
                                            Log.d("rxBle_", "Write 1 OK: " + new String(bytes));
                                            return rxBleConnection.writeCharacteristic(WRITE_CHARACTERISTIC_UUID, payloadPswd).timeout(3, TimeUnit.SECONDS);
                                        }).toObservable();
                            })
                            .retry(2)
                            .subscribe(
                                    bytes -> Log.d("rxBle_", "Write 2 OK: " + new String(bytes)),
                                    err -> Log.e("rxBle_", "Erro em algum write", err),
                                    () -> {}
                            );
                });

                AlertDialog dialog = builder.create();
                dialog.show();
            });

            return holder;
        }

        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

            RxBleDevice rxBleDevice = mListRxBleDevices.get(position);
            holder.viewText.setText(rxBleDevice.getName());
        }

        @Override
        public int getItemCount() {
            return mListRxBleDevices.size();
        }
    }

    @Override
    public void onViewCreated(@NonNull View view_, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view_, savedInstanceState);

        mProgressBar = view_.findViewById(R.id.progressBar);

        RecyclerView recyclerView = view_.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter( itemAdapter_ );

        mSwipeRefreshLayout = view_.findViewById(R.id.swipeRefresh);
        mSwipeRefreshLayout.setOnRefreshListener(() -> {

            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {

                fetchNearestData();
            });
        });
    }

    private void fetchNearestData() {

        RxBleClient rxBleClient = RxBleClient.create(getContext());

        mListRxBleDevices.clear();
        Disposable scanDisposable = rxBleClient.scanBleDevices(new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build())
                .take(3, TimeUnit.SECONDS)
                .filter(sr -> sr.getBleDevice().getName() != null
                        && !sr.getBleDevice().getName().isEmpty()
                        && sr.getBleDevice().getName().endsWith(".vmflow.xyz")
                        && !sr.getBleDevice().getName().equals("0.vmflow.xyz") )
                .distinct(sr -> sr.getBleDevice().getMacAddress()) // chave: MAC -> remove repetições
                .subscribe( sr -> {
                    Log.d("rxBle_", sr.getBleDevice().getName());

                    mListRxBleDevices.add(sr.getBleDevice());
                }, th -> {
                    Log.d("rxBle_", th.toString());
                }, () -> {

                    if(getActivity() != null)
                        getActivity().runOnUiThread(() -> {

                            itemAdapter_.notifyDataSetChanged();
                            mSwipeRefreshLayout.setRefreshing(false);

                            mProgressBar.setVisibility(View.GONE);

                            Log.d("rxBle_", "...GONE");

                        });

                    Log.d("rxBle_", "...end");
                });
    }

    @Override
    public void onStart() {
        super.onStart();

        mProgressBar.setVisibility(View.VISIBLE);

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            fetchNearestData();
        });
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_nearest, container, false);
    }
}