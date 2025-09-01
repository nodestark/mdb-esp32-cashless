package xyz.vmflow.target;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RadioGroup;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class RegisterActivity extends AppCompatActivity {

    private static final String SUPABASE_URL = "https://supabase.vmflow.xyz";
    private static final String SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_register);

        Button btnSubmit = findViewById(R.id.btnSubmit);
        btnSubmit.setOnClickListener(v -> registerUser());
    }

    private void registerUser() {

        EditText editName = findViewById(R.id.etName);
        EditText editEmail = findViewById(R.id.etEmail);
        EditText editPswd = findViewById(R.id.etPassword);

        String email = editEmail.getText().toString().trim();
        String senha = editPswd.getText().toString().trim();
        String nome = editName.getText().toString().trim();

        if (email.isEmpty() || senha.isEmpty()) {
            Toast.makeText(this, "Preencha todos os campos!", Toast.LENGTH_SHORT).show();
            return;
        }

        try {

            JSONObject json = new JSONObject();

            json.put("email", email);
            json.put("password", senha);

            JSONObject data = new JSONObject();

            data.put("full_name", nome);

            json.put("data", data);

            RequestBody requestBody = RequestBody.create( json.toString(), MediaType.parse("application/json") );

            Request request = new Request.Builder()
                    .url(SUPABASE_URL + "/auth/v1/signup")
                    .addHeader("apikey", SUPABASE_KEY)
                    .addHeader("Content-Type", "application/json")
                    .post(requestBody)
                    .build();

            new OkHttpClient().newCall(request).enqueue(new Callback() {

                @Override
                public void onResponse(@NonNull Call call, @NonNull Response response) throws IOException {

                    if (response.isSuccessful()) {

                        SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);

                        SharedPreferences.Editor editor = prefs.edit();
                        editor.putString("auth_json", response.body().string());
                        editor.apply();

                        Intent intent= new Intent(RegisterActivity.this, MainActivity.class);
                        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                        startActivity(intent);
                    } else {
                        runOnUiThread(() -> {
                            try {
                                JSONObject jsonObject = new JSONObject(response.body().string());

                                Toast.makeText(RegisterActivity.this, jsonObject.getString("msg"), Toast.LENGTH_SHORT).show();
                            } catch (IOException | JSONException e) {
                                e.printStackTrace();
                            }
                        });
                    }
                }

                @Override
                public void onFailure(@NonNull Call call, @NonNull IOException e) {
                    runOnUiThread(() -> Toast.makeText(RegisterActivity.this, "onFailure", Toast.LENGTH_SHORT).show() );
                }
            } );

        } catch (JSONException e) {
            throw new RuntimeException(e);
        }
    }
}