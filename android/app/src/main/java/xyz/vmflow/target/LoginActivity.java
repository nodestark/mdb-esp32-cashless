package xyz.vmflow.target;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
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

public class LoginActivity extends AppCompatActivity {

    private EditText editEmail, editPassword;
    private Button btnLogin;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);
        String authJson = prefs.getString("auth_json", null);

        if (authJson != null && !authJson.isEmpty()) {
            Intent intent = new Intent(LoginActivity.this, MainActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(intent);

            return;
        }

        setContentView(R.layout.activity_login);

        editEmail = findViewById(R.id.editEmail);
        editPassword = findViewById(R.id.editPassword);

        btnLogin = findViewById(R.id.btnLogin);
        btnLogin.setOnClickListener(v -> loginUser());

        Button btnRegister = findViewById(R.id.btnRegister);
        btnRegister.setOnClickListener(v -> registerUser());

        TextView txtForgotPassword = findViewById(R.id.txtForgotPassword);
        txtForgotPassword.setOnClickListener(v ->
                startActivity(new Intent(LoginActivity.this, ForgotPasswordActivity.class)));
    }

    private void registerUser(){
        startActivity(new Intent(LoginActivity.this, RegisterActivity.class));
    }

    private void loginUser() {
        String email = editEmail.getText().toString().trim();
        String senha = editPassword.getText().toString();

        if (email.isEmpty() || senha.isEmpty()) {
            Toast.makeText(this, "Please fill in all required fields.", Toast.LENGTH_SHORT).show();
            return;
        }

        if (!android.util.Patterns.EMAIL_ADDRESS.matcher(email).matches()) {
            Toast.makeText(this, "Please enter a valid email address", Toast.LENGTH_SHORT).show();
            return;
        }

        try {

            JSONObject json = new JSONObject();

            json.put("email", email);
            json.put("password", senha);

            RequestBody requestBody = RequestBody.create( json.toString(), MediaType.parse("application/json") );

            Request request = new Request.Builder()
                    .url(BuildConfig.SUPABASE_URL + "/auth/v1/token?grant_type=password")
                    .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                    .addHeader("Content-Type", "application/json")
                    .post(requestBody)
                    .build();

            new OkHttpClient().newCall(request).enqueue(new Callback() {

                @Override
                public void onResponse(@NonNull Call call, @NonNull Response response) throws IOException {

                    String body = response.body().string();

                    if (response.isSuccessful()) {

                        SharedPreferences prefs = getSharedPreferences("target_prefs", MODE_PRIVATE);
                        prefs.edit().putString("auth_json", body).apply();

                        Intent intent = new Intent(LoginActivity.this, MainActivity.class);
                        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                        startActivity(intent);
                    } else {
                        runOnUiThread(() -> {
                            try {
                                JSONObject jsonObject = new JSONObject(body);
                                Toast.makeText(LoginActivity.this, jsonObject.getString("msg"), Toast.LENGTH_SHORT).show();
                            } catch (JSONException e) {
                                Toast.makeText(LoginActivity.this, "Login failed. Please try again.", Toast.LENGTH_SHORT).show();
                            }
                        });
                    }
                }

                @Override
                public void onFailure(@NonNull Call call, @NonNull IOException e) {
                    runOnUiThread(() -> Toast.makeText(LoginActivity.this, "onFailure", Toast.LENGTH_SHORT).show() );
                }
            } );

        } catch (JSONException e) {
            throw new RuntimeException(e);
        }
    }
}