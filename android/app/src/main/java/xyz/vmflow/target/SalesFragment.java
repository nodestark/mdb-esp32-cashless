package xyz.vmflow.target;

import android.content.Context;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.text.NumberFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

import okhttp3.Request;
import okhttp3.Response;
import xyz.vmflow.R;

public class SalesFragment extends Fragment {


    private final List<JSONObject> mListSales = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    private View mProgressBar;
    private TextView tvSalesToday;
    private TextView tvMachinesOnline;

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

        TextView saleChannelText;
        TextView saleEmbeddedSubdomainText;
        TextView saleItemNumberText;
        TextView saleItemPriceText;
        TextView saleCreatedAtText;

        public ViewHolder_(@NonNull View itemView) {
            super(itemView);

            saleChannelText = itemView.findViewById(R.id.textViewChannel);
            saleEmbeddedSubdomainText = itemView.findViewById(R.id.textViewMachineId);
            saleItemNumberText = itemView.findViewById(R.id.textViewItemNumber);
            saleItemPriceText = itemView.findViewById(R.id.textViewItemPrice);
            saleCreatedAtText = itemView.findViewById(R.id.textViewDateTime);
        }
    }

    class ItemAdapter_ extends RecyclerView.Adapter<ViewHolder_> {

        private final NumberFormat currencyFormat = NumberFormat.getCurrencyInstance(Locale.getDefault());
        private final SimpleDateFormat isoParser = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssXXX", Locale.US);
        private final SimpleDateFormat displayFormat = new SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.getDefault());

        public ViewHolder_ onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_sale_layout, parent, false);
            return new ViewHolder_(view);
        }

        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

            JSONObject jsonEmbedded = mListSales.get(position);

            holder.saleChannelText.setText(jsonEmbedded.optString("channel", "-"));

            // item_number is NULL on a pending MQTT sale until the device reports the vend;
            // show "-" instead of letting getInt() throw and leave the layout placeholder showing.
            if (jsonEmbedded.isNull("item_number")) {
                holder.saleItemNumberText.setText("Item #-");
            } else {
                holder.saleItemNumberText.setText(String.format("Item #%03x", jsonEmbedded.optInt("item_number")));
            }

            JSONObject embedded = jsonEmbedded.optJSONObject("embedded");
            if (embedded != null && !embedded.isNull("subdomain")) {
                holder.saleEmbeddedSubdomainText.setText(String.format("Embedded: %06d", embedded.optInt("subdomain")));
            } else {
                holder.saleEmbeddedSubdomainText.setText("Embedded: -");
            }

            String rawDate = jsonEmbedded.optString("created_at", "");
            try {
                // Supabase returns microseconds (e.g. 2026-04-20T14:30:45.123456+00:00); strip fractional part before parsing
                String normalizedDate = rawDate.replaceFirst("\\.\\d+", "");
                holder.saleCreatedAtText.setText(displayFormat.format(isoParser.parse(normalizedDate)));
            } catch (java.text.ParseException e) {
                holder.saleCreatedAtText.setText(rawDate);
            }

            holder.saleItemPriceText.setText(currencyFormat.format(jsonEmbedded.optDouble("item_price", 0)));
        }

        @Override
        public int getItemCount() {
            return mListSales.size();
        }
    }

    @Override
    public void onViewCreated(@NonNull View view_, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view_, savedInstanceState);

        mProgressBar = view_.findViewById(R.id.progressBar);
        tvSalesToday = view_.findViewById(R.id.tvSalesToday);
        tvMachinesOnline = view_.findViewById(R.id.tvMachinesOnline);

        RecyclerView recyclerView = view_.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter(itemAdapter_);

        SwipeRefreshLayout swipeRefreshLayout = view_.findViewById(R.id.swipeRefresh);
        swipeRefreshLayout.setOnRefreshListener(() -> {
            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {
                fetchSalesData();
                fetchMetrics();
                ui(() -> swipeRefreshLayout.setRefreshing(false));
            });
        });
    }

    private String isoTimestamp(Calendar cal) {
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssXXX", Locale.US);
        return sdf.format(cal.getTime());
    }

    private void fetchMetrics() {
        if (!isAdded()) return;
        try {
            Calendar now = Calendar.getInstance();

            Calendar todayStart = (Calendar) now.clone();
            todayStart.set(Calendar.HOUR_OF_DAY, 0);
            todayStart.set(Calendar.MINUTE, 0);
            todayStart.set(Calendar.SECOND, 0);
            todayStart.set(Calendar.MILLISECOND, 0);

            // Sales today
            Request reqToday = Api.request("/rest/v1/sales?select=item_price&created_at=gte." + isoTimestamp(todayStart)).build();

            // Machines online (embedded linked to a machine)
            Request reqEmbedded = Api.request("/rest/v1/embedded?select=machine_id,status&machine_id=not.is.null").build();

            ExecutorService pool = Executors.newFixedThreadPool(2);
            Future<Double> futureToday = pool.submit(() -> sumItemPrices(Api.client(mAppContext).newCall(reqToday).execute()));
            Future<int[]> futureMachines = pool.submit(() -> parseMachinesCounts(Api.client(mAppContext).newCall(reqEmbedded).execute()));
            pool.shutdown();

            double salesToday = futureToday.get();
            int[] machinesCounts = futureMachines.get();

            NumberFormat currencyFormat = NumberFormat.getCurrencyInstance(Locale.getDefault());
            String todayStr = currencyFormat.format(salesToday);
            String onlineStr = machinesCounts[0] + " / " + machinesCounts[1];

            ui(() -> {
                tvSalesToday.setText(todayStr);
                tvMachinesOnline.setText(onlineStr);
            });

        } catch (InterruptedException | java.util.concurrent.ExecutionException e) {
            e.printStackTrace();
        }
    }

    private double sumItemPrices(Response response) throws IOException, JSONException {
        if (!response.isSuccessful()) return 0;
        JSONArray arr = new JSONArray(response.body().string());
        double total = 0;
        for (int i = 0; i < arr.length(); i++) {
            total += arr.getJSONObject(i).optDouble("item_price", 0);
        }
        return total;
    }

    // returns [online, total]
    private int[] parseMachinesCounts(Response response) throws IOException, JSONException {
        if (!response.isSuccessful()) return new int[]{0, 0};
        JSONArray arr = new JSONArray(response.body().string());
        int total = arr.length();
        int online = 0;
        for (int i = 0; i < arr.length(); i++) {
            if ("online".equals(arr.getJSONObject(i).optString("status"))) online++;
        }
        return new int[]{online, total};
    }

    private void fetchSalesData() {
        if (!isAdded()) return;

        try {
            Request request = Api.request("/rest/v1/sales?select=*,embedded(subdomain)&order=created_at.desc&limit=50").build();
            try (Response response = Api.client(mAppContext).newCall(request).execute()) {
                String body = response.body().string();

                if (response.isSuccessful()) {
                    JSONArray jsonArray = new JSONArray(body);

                    // Build off-thread, swap on the UI thread: mutating the adapter's list from a
                    // background thread crashes RecyclerView ("Inconsistency detected").
                    List<JSONObject> fresh = new ArrayList<>();
                    for (int i = 0; i < jsonArray.length(); i++) {
                        fresh.add(jsonArray.getJSONObject(i));
                    }

                    ui(() -> {
                        mListSales.clear();
                        mListSales.addAll(fresh);
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
            e.printStackTrace();
        }

        ui(() -> mProgressBar.setVisibility(View.GONE));
    }

    @Override
    public void onStart() {
        super.onStart();

        mProgressBar.setVisibility(View.VISIBLE);

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            fetchSalesData();
            fetchMetrics();
        });
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_sales, container, false);
    }
}
