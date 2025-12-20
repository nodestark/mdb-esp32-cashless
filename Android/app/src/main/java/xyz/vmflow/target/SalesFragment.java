package xyz.vmflow.target;

import android.content.Context;
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
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import xyz.vmflow.R;

public class SalesFragment extends Fragment {

    private final List<JSONObject> mListSales = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    private static final String SUPABASE_URL = "https://supabase.vmflow.xyz";
    private static final String SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4";
    private View mProgressBar;

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

        public ViewHolder_ onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_sale_layout, parent, false);
            return new ViewHolder_(view);
        }

        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

            JSONObject jsonEmbedded = mListSales.get(position);
            try {
                holder.saleChannelText.setText(jsonEmbedded.getString("channel"));
                holder.saleItemNumberText.setText(String.format("Item #%03x", jsonEmbedded.getInt("item_number")) );
                holder.saleEmbeddedSubdomainText.setText(String.format("Embedded: %06d", jsonEmbedded.getJSONObject("embeddeds").getInt("subdomain")) );
                holder.saleCreatedAtText.setText( jsonEmbedded.getString("created_at") );

                NumberFormat currencyFormat = NumberFormat.getCurrencyInstance(Locale.getDefault());
                String formattedPrice = currencyFormat.format(jsonEmbedded.getDouble("item_price"));
                holder.saleItemPriceText.setText( formattedPrice );

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

        RecyclerView recyclerView = view_.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter( itemAdapter_ );

        SwipeRefreshLayout swipeRefreshLayout = view_.findViewById(R.id.swipeRefresh);
        swipeRefreshLayout.setOnRefreshListener(() -> {

            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {

                fetchSalesData();

                if(getActivity() != null)
                    getActivity().runOnUiThread(() -> swipeRefreshLayout.setRefreshing(false));
            });
        });
    }

    private void fetchSalesData() {

        boolean retry;
        do {
            retry = false; // controla se precisa repetir a requisição

            try {
                SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);

                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                Request request = new Request.Builder()
                        .url(SUPABASE_URL + "/rest/v1/sales?select=*,embeddeds(subdomain)&order=created_at.desc")
                        .addHeader("apikey", SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                        .addHeader("Content-Type", "application/json")
                        .build();

                Response response = new OkHttpClient().newCall(request).execute();
                if(response.isSuccessful()){

                    try {
                        JSONArray jsonArray = new JSONArray(response.body().string());

                        mListSales.clear();
                        for (int i = 0; i < jsonArray.length(); i++) {
                            mListSales.add(jsonArray.getJSONObject(i));
                        }

                        if(getActivity() != null)
                            getActivity().runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());

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
                    getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Erro: " + response.code(), Toast.LENGTH_SHORT).show());
                }

            } catch (JSONException | IOException e) {
                e.printStackTrace();
            }

        } while(retry);

        if(getActivity() != null)
            getActivity().runOnUiThread(() -> mProgressBar.setVisibility(View.GONE) );
    }
    @Override
    public void onStart() {
        super.onStart();

        mProgressBar.setVisibility(View.VISIBLE);

        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            fetchSalesData();
        });
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_sales, container, false);
    }
}