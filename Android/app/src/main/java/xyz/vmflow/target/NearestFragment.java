package xyz.vmflow.target;

import android.app.AlertDialog;
import android.content.Context;
import android.net.wifi.WifiManager;
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

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.polidea.rxandroidble3.RxBleClient;
import com.polidea.rxandroidble3.RxBleDevice;
import com.polidea.rxandroidble3.scan.ScanSettings;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import io.reactivex.rxjava3.disposables.Disposable;

public class NearestFragment extends Fragment {

    private static final UUID WRITE_CHARACTERISTIC_UUID = UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002");

    private final List<RxBleDevice> mListRxBleDevices = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

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

        public ViewHolder_ onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_nearest_layout, parent, false);
            return new ViewHolder_(view);
        }

        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

            RxBleDevice rxBleDevice = mListRxBleDevices.get(position);
            holder.viewText.setText(rxBleDevice.getName());

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
            });
        }

        @Override
        public int getItemCount() {
            return mListRxBleDevices.size();
        }
    }

    @Override
    public void onViewCreated(@NonNull View view_, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view_, savedInstanceState);

        RecyclerView recyclerView = view_.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter( itemAdapter_ );

        SwipeRefreshLayout swipeRefreshLayout = view_.findViewById(R.id.swipeRefresh);
        swipeRefreshLayout.setOnRefreshListener(() -> {

            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {

                fetchNearestData();

                if(getActivity() != null)
                    getActivity().runOnUiThread(() -> swipeRefreshLayout.setRefreshing(false));
            });
        });
    }

    private void fetchNearestData() {

        RxBleClient rxBleClient = RxBleClient.create(getContext());

        mListRxBleDevices.clear();
        Disposable scanDisposable = rxBleClient.scanBleDevices(new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build())
                .take(3, TimeUnit.SECONDS)
                .filter(sr -> sr.getBleDevice().getName() != null && !sr.getBleDevice().getName().isEmpty())
                .distinct(sr -> sr.getBleDevice().getMacAddress()) // chave: MAC -> remove repetições
                .subscribe( sr -> {
                    Log.d("rxBle_", sr.getBleDevice().getName());

                    mListRxBleDevices.add(sr.getBleDevice());

                    if(getActivity() != null)
                        getActivity().runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());

                }, th -> {
                    Log.d("rxBle_", th.toString());
                }, () -> {

                    Log.d("rxBle_", "...end");
                });
    }

    @Override
    public void onStart() {
        super.onStart();

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            fetchNearestData();

            if(getActivity() != null)
                getActivity().runOnUiThread(() -> {
                    ProgressBar progressBar = getActivity().findViewById(R.id.progressBar);
                    progressBar.setVisibility(View.GONE);
                } );
        });
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_nearest, container, false);
    }
}