package xyz.vmflow.target;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
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

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import io.reactivex.rxjava3.disposables.Disposable;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class NearestFragment extends Fragment {

    private final List<RxBleDevice> mListRxBleDevices = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    class ViewHolder_ extends RecyclerView.ViewHolder {

        TextView viewText;

        public ViewHolder_(@NonNull View itemView) {
            super(itemView);

            viewText = itemView.findViewById(R.id.textViewNearest);
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