package xyz.vmflow.target;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.google.android.material.floatingactionbutton.FloatingActionButton;


import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import xyz.vmflow.BuildConfig;
import xyz.vmflow.R;

public class MachinesFragment extends Fragment {

    private static final OkHttpClient HTTP_CLIENT = new OkHttpClient();
    private static final String[] CATEGORIES = {"Snack", "Drink", "Frozen", "Candy", "Personal Care", "Other"};

    private final List<JSONObject> mListMachines = new ArrayList<>();
    private final ItemAdapter_ itemAdapter_ = new ItemAdapter_();

    private View mProgressBar;

    class ViewHolder_ extends RecyclerView.ViewHolder {
        TextView tvName, tvCategory, tvLocation;
        ImageButton btnEdit;

        public ViewHolder_(@NonNull View itemView) {
            super(itemView);
            tvName = itemView.findViewById(R.id.textViewMachineName);
            tvCategory = itemView.findViewById(R.id.textViewMachineCategory);
            tvLocation = itemView.findViewById(R.id.textViewMachineLocation);
            btnEdit = itemView.findViewById(R.id.btnEditMachine);
        }
    }

    class ItemAdapter_ extends RecyclerView.Adapter<ViewHolder_> {

        public ViewHolder_ onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_machine_layout, parent, false);
            ViewHolder_ holder = new ViewHolder_(view);

            holder.btnEdit.setOnClickListener(v -> {
                int pos = holder.getAdapterPosition();
                if (pos != RecyclerView.NO_ID)
                    showMachineFormDialog(mListMachines.get(pos));
            });

            return holder;
        }

        public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {
            JSONObject machine = mListMachines.get(position);
            holder.tvName.setText(opt(machine, "name"));
            holder.tvCategory.setText("Category: " + opt(machine, "category"));
            holder.tvLocation.setText("Location: " + opt(machine, "location_name"));
        }

        @Override
        public int getItemCount() {
            return mListMachines.size();
        }
    }

    @Override
    public void onViewCreated(@NonNull View view_, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view_, savedInstanceState);

        mProgressBar = view_.findViewById(R.id.progressBar);

        RecyclerView recyclerView = view_.findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        recyclerView.setAdapter(itemAdapter_);

        FloatingActionButton fab = view_.findViewById(R.id.fabAdd);
        fab.setOnClickListener(v -> showMachineFormDialog(null));

        SwipeRefreshLayout swipeRefreshLayout = view_.findViewById(R.id.swipeRefresh);
        swipeRefreshLayout.setOnRefreshListener(() -> {
            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {
                fetchMachinesData();
                if (getActivity() != null)
                    getActivity().runOnUiThread(() -> swipeRefreshLayout.setRefreshing(false));
            });
        });
    }

    private void showMachineFormDialog(@Nullable JSONObject machine) {
        if (getContext() == null) return;

        View formView = LayoutInflater.from(getContext()).inflate(R.layout.dialog_machine_form, null);
        EditText etName = formView.findViewById(R.id.editTextMachineName);
        Spinner spinnerCategory = formView.findViewById(R.id.spinnerCategory);
        EditText etLocation = formView.findViewById(R.id.editTextMachineLocation);

        ArrayAdapter<String> spinnerAdapter = new ArrayAdapter<>(getContext(), android.R.layout.simple_spinner_item, CATEGORIES);
        spinnerAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinnerCategory.setAdapter(spinnerAdapter);

        if (machine != null) {
            etName.setText(optField(machine, "name"));
            etLocation.setText(optField(machine, "location_name"));
            String category = machine.optString("category", "");
            for (int i = 0; i < CATEGORIES.length; i++) {
                if (CATEGORIES[i].equals(category)) {
                    spinnerCategory.setSelection(i);
                    break;
                }
            }
        }

        AlertDialog dialog = new AlertDialog.Builder(getContext())
                .setTitle(machine == null ? "New machine" : "Edit machine")
                .setView(formView)
                .setPositiveButton(machine == null ? "Create" : "Save", null)
                .setNegativeButton("Cancel", null)
                .create();

        dialog.show();

        // Override positive button to validate before dismissing
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String name = etName.getText().toString().trim();
            if (name.isEmpty()) {
                etName.setError("Required");
                return;
            }
            String category = spinnerCategory.getSelectedItem().toString();
            String location = etLocation.getText().toString().trim();
            dialog.dismiss();

            ExecutorService executor = Executors.newSingleThreadExecutor();
            executor.execute(() -> {
                if (machine == null)
                    createMachine(name, category, location);
                else
                    updateMachine(machine.optString("id", ""), name, category, location, machine);
            });
        });
    }

    private void createMachine(String name, String category, String location) {
        boolean retry;
        do {
            retry = false;
            try {
                SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);
                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                JSONObject body = new JSONObject();
                body.put("name", name);
                body.put("category", category);
                if (!location.isEmpty()) body.put("location_name", location);

                Request request = new Request.Builder()
                        .url(BuildConfig.SUPABASE_URL + "/rest/v1/machines")
                        .post(RequestBody.create(body.toString(), MediaType.parse("application/json")))
                        .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                        .addHeader("Content-Type", "application/json")
                        .addHeader("Prefer", "return=representation")
                        .build();

                Response response = HTTP_CLIENT.newCall(request).execute();
                if (response.isSuccessful()) {
                    JSONArray arr = new JSONArray(response.body().string());
                    JSONObject created = arr.getJSONObject(0);
                    mListMachines.add(created);
                    if (getActivity() != null)
                        getActivity().runOnUiThread(() -> {
                            itemAdapter_.notifyItemInserted(mListMachines.size() - 1);
                            Toast.makeText(getContext(), "Machine created", Toast.LENGTH_SHORT).show();
                        });
                } else if (response.code() == 401) {
                    retry = refreshToken(prefs, jsonAuth);
                } else {
                    if (getActivity() != null)
                        getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Error: " + response.code(), Toast.LENGTH_SHORT).show());
                }

            } catch (JSONException | IOException e) {
                e.printStackTrace();
            }
        } while (retry);
    }

    private void updateMachine(String id, String name, String category, String location, JSONObject original) {
        boolean retry;
        do {
            retry = false;
            try {
                SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);
                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                JSONObject body = new JSONObject();
                body.put("name", name);
                body.put("category", category);
                body.put("location_name", location.isEmpty() ? JSONObject.NULL : location);

                Request request = new Request.Builder()
                        .url(BuildConfig.SUPABASE_URL + "/rest/v1/machines?id=eq." + id)
                        .patch(RequestBody.create(body.toString(), MediaType.parse("application/json")))
                        .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                        .addHeader("Content-Type", "application/json")
                        .build();

                Response response = HTTP_CLIENT.newCall(request).execute();
                if (response.isSuccessful()) {
                    original.put("name", name);
                    original.put("category", category);
                    original.put("location_name", location);
                    int pos = mListMachines.indexOf(original);
                    if (getActivity() != null) {
                        int finalPos = pos;
                        getActivity().runOnUiThread(() -> {
                            if (finalPos >= 0) itemAdapter_.notifyItemChanged(finalPos);
                            Toast.makeText(getContext(), "Saved", Toast.LENGTH_SHORT).show();
                        });
                    }
                } else if (response.code() == 401) {
                    retry = refreshToken(prefs, jsonAuth);
                } else {
                    if (getActivity() != null)
                        getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "Error: " + response.code(), Toast.LENGTH_SHORT).show());
                }

            } catch (JSONException | IOException e) {
                e.printStackTrace();
            }
        } while (retry);
    }

    private void fetchMachinesData() {
        boolean retry;
        do {
            retry = false;
            try {
                SharedPreferences prefs = requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE);
                JSONObject jsonAuth = new JSONObject(prefs.getString("auth_json", "{}"));

                Request request = new Request.Builder()
                        .url(BuildConfig.SUPABASE_URL + "/rest/v1/machines?select=id,name,category,location_name&order=name")
                        .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                        .addHeader("Authorization", "Bearer " + jsonAuth.getString("access_token"))
                        .build();

                Response response = HTTP_CLIENT.newCall(request).execute();
                if (response.isSuccessful()) {
                    JSONArray arr = new JSONArray(response.body().string());
                    mListMachines.clear();
                    for (int i = 0; i < arr.length(); i++)
                        mListMachines.add(arr.getJSONObject(i));
                    if (getActivity() != null)
                        getActivity().runOnUiThread(() -> itemAdapter_.notifyDataSetChanged());
                } else if (response.code() == 401) {
                    retry = refreshToken(prefs, jsonAuth);
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

    private static String opt(JSONObject obj, String key) {
        String val = obj.optString(key, "");
        return (val.isEmpty() || val.equals("null")) ? "—" : val;
    }

    private static String optField(JSONObject obj, String key) {
        String val = obj.optString(key, "");
        return val.equals("null") ? "" : val;
    }

    private boolean refreshToken(SharedPreferences prefs, JSONObject jsonAuth) {
        try {
            RequestBody body = RequestBody.create(jsonAuth.toString(), MediaType.parse("application/json"));
            Request req = new Request.Builder()
                    .url(BuildConfig.SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token")
                    .post(body)
                    .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                    .addHeader("Content-Type", "application/json")
                    .build();
            Response res = HTTP_CLIENT.newCall(req).execute();
            if (res.isSuccessful()) {
                prefs.edit().putString("auth_json", res.body().string()).apply();
                return true;
            } else {
                redirectToLogin();
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
        return false;
    }

    private void redirectToLogin() {
        if (getActivity() == null) return;
        requireContext().getSharedPreferences("target_prefs", Context.MODE_PRIVATE)
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
        executor.execute(this::fetchMachinesData);
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_machines, container, false);
    }
}
