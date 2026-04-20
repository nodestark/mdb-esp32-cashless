package xyz.vmflow.target;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
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
import xyz.vmflow.BuildConfig;
import xyz.vmflow.R;

public class RegisterActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_register);

        Button btnSubmit = findViewById(R.id.btnSubmit);
        btnSubmit.setOnClickListener(v -> registerUser());
    }

    private void registerUser() {

        EditText editEmail = findViewById(R.id.etEmail);
        EditText editPswd = findViewById(R.id.etPassword);
        EditText editConfirmPswd = findViewById(R.id.etConfirmPassword);

        String email = editEmail.getText().toString().trim();
        String senha = editPswd.getText().toString();
        String confirmPassword = editConfirmPswd.getText().toString();

        if (email.isEmpty() || senha.isEmpty()) {
            Toast.makeText(this, "Please fill in all required fields", Toast.LENGTH_SHORT).show();
            return;
        }

        if (!android.util.Patterns.EMAIL_ADDRESS.matcher(email).matches()) {
            Toast.makeText(this, "Please enter a valid email address", Toast.LENGTH_SHORT).show();
            return;
        }

        if (!senha.equals(confirmPassword)) {
            Toast.makeText(this, "Passwords do not match", Toast.LENGTH_SHORT).show();
            return;
        }

        try {

            JSONObject json = new JSONObject();

            json.put("email", email);
            json.put("password", senha);

            RequestBody requestBody = RequestBody.create( json.toString(), MediaType.parse("application/json") );

            Request request = new Request.Builder()
                    .url(BuildConfig.SUPABASE_URL + "/auth/v1/signup")
                    .addHeader("apikey", BuildConfig.SUPABASE_KEY)
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

                        Intent intent = new Intent(RegisterActivity.this, MainActivity.class);
                        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                        startActivity(intent);
                    } else {
                        runOnUiThread(() -> {
                            try {
                                JSONObject jsonObject = new JSONObject(response.body().string());
                                Toast.makeText(RegisterActivity.this, jsonObject.getString("msg"), Toast.LENGTH_SHORT).show();
                            } catch (JSONException | IOException e) {
                                Toast.makeText(RegisterActivity.this, "Registration failed. Please try again.", Toast.LENGTH_SHORT).show();
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