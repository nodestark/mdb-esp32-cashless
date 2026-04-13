package xyz.vmflow.target;

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

public class ForgotPasswordActivity extends AppCompatActivity {

    private EditText editEmail;
    private Button btnSendReset;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_forgot_password);

        editEmail = findViewById(R.id.editEmail);
        btnSendReset = findViewById(R.id.btnSendReset);

        btnSendReset.setOnClickListener(v -> sendResetEmail());

        Button btnBackToLogin = findViewById(R.id.btnBackToLogin);
        btnBackToLogin.setOnClickListener(v -> finish());
    }

    private void sendResetEmail() {
        String email = editEmail.getText().toString().trim();

        if (email.isEmpty()) {
            Toast.makeText(this, "Please enter your email.", Toast.LENGTH_SHORT).show();
            return;
        }

        btnSendReset.setEnabled(false);

        try {

            JSONObject json = new JSONObject();
            json.put("email", email);

            RequestBody requestBody = RequestBody.create(json.toString(), MediaType.parse("application/json"));

            Request request = new Request.Builder()
                    .url(BuildConfig.SUPABASE_URL + "/auth/v1/recover")
                    .addHeader("apikey", BuildConfig.SUPABASE_KEY)
                    .addHeader("Content-Type", "application/json")
                    .post(requestBody)
                    .build();

            new OkHttpClient().newCall(request).enqueue(new Callback() {

                @Override
                public void onResponse(@NonNull Call call, @NonNull Response response) throws IOException {
                    runOnUiThread(() -> {
                        btnSendReset.setEnabled(true);

                        if (response.isSuccessful()) {
                            Toast.makeText(ForgotPasswordActivity.this,
                                    "Check your email for the reset link.", Toast.LENGTH_LONG).show();
                        } else {
                            Toast.makeText(ForgotPasswordActivity.this,
                                    "Error sending reset email. Try again.", Toast.LENGTH_SHORT).show();
                        }
                    });
                }

                @Override
                public void onFailure(@NonNull Call call, @NonNull IOException e) {
                    runOnUiThread(() -> {
                        btnSendReset.setEnabled(true);
                        Toast.makeText(ForgotPasswordActivity.this,
                                "Connection error. Try again.", Toast.LENGTH_SHORT).show();
                    });
                }
            });

        } catch (JSONException e) {
            btnSendReset.setEnabled(false);
            throw new RuntimeException(e);
        }
    }
}
