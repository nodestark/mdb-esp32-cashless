package xyz.vmflow.target;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
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

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import xyz.vmflow.BuildConfig;
import xyz.vmflow.R;

public class SalesFragment extends Fragment {

    private static final OkHttpClient HTTP_CLIENT = new OkHttpClient();

    private final List<JSONObject> mListSales = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    private View mProgressBar;
    private TextView tvSalesToday;
    private TextView tvMachinesOnline;

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
            try {
                holder.saleChannelText.setText(jsonEmbedded.getString("channel"));
                holder.saleItemNumberText.setText(String.format("Item #%03x", jsonEmbedded.getInt("item_number")));
                holder.saleEmbeddedSubdomainText.setText(String.format("Embedded: %06d", jsonEmbedded.getJSONObject("embedded").getInt("subdomain")));

                String rawDate = jsonEmbedded.getString("created_at");
                try {
                    // Supabase returns microseconds (e.g. 2026-04-20T14:30:45.123456+00:00); strip fractional part before parsing
                    String normalizedDate = rawDate.replaceFirst("\\.\\d+", "");
                    holder.saleCreatedAtText.setText(displayFormat.format(isoParser.parse(normalizedDate)));
                } catch (java.text.ParseException e) {
                    holder.saleCreatedAtText.setText(rawDate);
                }

                String formattedPrice = currencyFormat.format(jsonEmbedded.getDouble("item_price"));
                holder.saleItemPriceText.setText(formattedPrice);

            } catch (JSONException e) {
                e.printStackTrace();
            }
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
                if (getActivity() != null)
                    getActivity().runOnUiThread(() -> swipeRefreshLayout.setRefreshing(false));
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
            SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);
            JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));
            String bearer = "Bearer " + jsonAuth.getString("access_token");

            Calendar now = Calendar.getInstance();

            Calendar todayStart = (Calendar) now.clone();
            todayStart.set(Calendar.HOUR_OF_DAY, 0);
            todayStart.set(Calendar.MINUTE, 0);
            todayStart.set(Calendar.SECOND, 0);
            todayStart.set(Calendar.MILLISECOND, 0);

            // Sales today
            Request reqToday = new Request.Builder()
                    .url(BuildConfig.SUPABASE_URL + "/rest/v1/sales?select=item_price&created_at=gte." + isoTimestamp(todayStart))
                    .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                    .addHeader("Authorization", bearer)
                    .build();

            // Machines online (embedded linked to a machine)
            Request reqEmbedded = new Request.Builder()
                    .url(BuildConfig.SUPABASE_URL + "/rest/v1/embedded?select=machine_id,status&machine_id=not.is.null")
                    .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                    .addHeader("Authorization", bearer)
                    .build();

            ExecutorService pool = Executors.newFixedThreadPool(2);
            Future<Double> futureToday = pool.submit(() -> sumItemPrices(HTTP_CLIENT.newCall(reqToday).execute()));
            Future<int[]> futureMachines = pool.submit(() -> parseMachinesCounts(HTTP_CLIENT.newCall(reqEmbedded).execute()));
            pool.shutdown();

            double salesToday = futureToday.get();
            int[] machinesCounts = futureMachines.get();

            NumberFormat currencyFormat = NumberFormat.getCurrencyInstance(Locale.getDefault());
            String todayStr = currencyFormat.format(salesToday);
            String onlineStr = machinesCounts[0] + " / " + machinesCounts[1];

            if (getActivity() != null) {
                getActivity().runOnUiThread(() -> {
                    tvSalesToday.setText(todayStr);
                    tvMachinesOnline.setText(onlineStr);
                });
            }

        } catch (JSONException | InterruptedException | java.util.concurrent.ExecutionException e) {
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

        boolean retry;
        do {
            retry = false;

            try {
                SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);

                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                Request request = new Request.Builder()
                        .url(BuildConfig.SUPABASE_URL + "/rest/v1/sales?select=*,embedded(subdomain)&order=created_at.desc&limit=50")
                        .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                        .addHeader("Content-Type", "application/json")
                        .build();

                Response response = HTTP_CLIENT.newCall(request).execute();
                String body = response.body().string();

                if (response.isSuccessful()) {
                    try {
                        JSONArray jsonArray = new JSONArray(body);

                        mListSales.clear();
                        for (int i = 0; i < jsonArray.length(); i++) {
                            mListSales.add(jsonArray.getJSONObject(i));
                        }

                        if (getActivity() != null)
                            getActivity().runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());

                    } catch (JSONException e) {
                        e.printStackTrace();
                    }

                } else if (response.code() == 401) {

                    RequestBody requestBody = RequestBody.create(jsonAuth.toString(), MediaType.parse("application/json"));

                    Request request_ = new Request.Builder()
                            .url(BuildConfig.SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                            .post(requestBody)
                            .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                            .addHeader("Content-Type", "application/json")
                            .build();

                    Response response_ = HTTP_CLIENT.newCall(request_).execute();
                    if (response_.isSuccessful()) {
                        SharedPreferences.Editor editor = prefs.edit();
                        editor.putString("auth_json", response_.body().string());
                        editor.apply();
                        retry = true;
                    } else {
                        redirectToLogin();
                    }
                } else {
                    if (getActivity() != null)
                        getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Error: " + response.code(), Toast.LENGTH_SHORT).show());
                }

            } catch (JSONException | IOException e) {
                e.printStackTrace();
            }

        } while (retry);

        if (getActivity() != null)
            getActivity().runOnUiThread(() -> mProgressBar.setVisibility(View.GONE));
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
        return inflater.inflate(R.layout.fragment_sales, container, false);
    }
}
