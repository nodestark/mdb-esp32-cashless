package xyz.vmflow.target;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.util.Log;

import androidx.annotation.Nullable;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import xyz.vmflow.BuildConfig;

/**
 * Shared Supabase HTTP client.
 *
 * Every request gets the anon {@code apikey} and the stored {@code Bearer} token. A 401 triggers one
 * refresh-token exchange (shared by all threads) and the request is replayed once with the new
 * token. If the refresh itself fails the stored session is cleared and the caller sees the 401,
 * which it should treat as "session expired" ({@link #isSessionExpired(Context, Response)}).
 */
public final class Api {

    private static final String TAG = "Api";
    private static final String PREFS = "target_prefs";
    private static final String KEY_AUTH = "auth_json";
    public static final MediaType JSON = MediaType.parse("application/json");

    private static volatile OkHttpClient sClient;
    // Plain client (no auth interceptor/authenticator) for the refresh call itself, so a 401 there
    // can never recurse into another refresh.
    private static volatile OkHttpClient sPlain;
    private static final Object REFRESH_LOCK = new Object();

    private Api() {}

    public static OkHttpClient client(Context context) {
        OkHttpClient c = sClient;
        if (c != null) return c;
        synchronized (Api.class) {
            if (sClient == null) {
                Context app = context.getApplicationContext();
                sPlain = new OkHttpClient.Builder()
                        .connectTimeout(10, TimeUnit.SECONDS)
                        .readTimeout(15, TimeUnit.SECONDS)
                        .callTimeout(20, TimeUnit.SECONDS)
                        .build();
                sClient = sPlain.newBuilder()
                        .addInterceptor(chain -> {
                            Request req = chain.request();
                            Request.Builder b = req.newBuilder();
                            if (req.header("apikey") == null) b.header("apikey", BuildConfig.SUPABASE_KEY);
                            if (req.header("Authorization") == null) {
                                String token = accessToken(app);
                                if (token != null) b.header("Authorization", "Bearer " + token);
                            }
                            return chain.proceed(b.build());
                        })
                        .authenticator((route, response) -> {
                            // Only one replay per request; a second 401 means the token is not the problem.
                            if (response.priorResponse() != null) return null;
                            String sent = response.request().header("Authorization");
                            String fresh = refreshIfStale(app, sent);
                            if (fresh == null) return null;
                            return response.request().newBuilder()
                                    .header("Authorization", "Bearer " + fresh)
                                    .build();
                        })
                        .build();
            }
            return sClient;
        }
    }

    public static Request.Builder request(String path) {
        return new Request.Builder().url(BuildConfig.SUPABASE_URL + path);
    }

    public static RequestBody json(JSONObject body) {
        return RequestBody.create(body.toString(), JSON);
    }

    /** True when a 401 survived the automatic refresh and the session was cleared (must log in again). */
    public static boolean isSessionExpired(Context context, Response response) {
        return response.code() == 401 && accessToken(context) == null;
    }

    /**
     * Human-readable reason from a Supabase / edge-function error body, falling back to the HTTP code.
     */
    public static String errorMessage(Response response, @Nullable String body) {
        if (body != null && !body.isEmpty()) {
            try {
                JSONObject j = new JSONObject(body);
                for (String k : new String[]{"error", "message", "msg", "error_description"}) {
                    String v = j.optString(k, "");
                    if (!v.isEmpty() && !"null".equals(v)) return v;
                }
            } catch (JSONException ignored) {
                if (body.length() <= 120) return body;
            }
        }
        return "HTTP " + response.code();
    }

    @Nullable
    static String accessToken(Context context) {
        try {
            String raw = prefs(context).getString(KEY_AUTH, null);
            if (raw == null) return null;
            String t = new JSONObject(raw).optString("access_token", "");
            return t.isEmpty() ? null : t;
        } catch (JSONException e) {
            return null;
        }
    }

    /**
     * Returns a token newer than {@code sentHeader}: either one another thread already refreshed,
     * or a freshly exchanged one. Null when the refresh failed (session cleared).
     */
    @Nullable
    private static String refreshIfStale(Context context, @Nullable String sentHeader) {
        synchronized (REFRESH_LOCK) {
            String current = accessToken(context);
            if (current != null && sentHeader != null && !sentHeader.equals("Bearer " + current)) {
                return current; // refreshed meanwhile by another request
            }
            try {
                String raw = prefs(context).getString(KEY_AUTH, null);
                String refreshToken = raw == null ? "" : new JSONObject(raw).optString("refresh_token", "");
                if (refreshToken.isEmpty()) {
                    clearSession(context);
                    return null;
                }
                JSONObject body = new JSONObject().put("refresh_token", refreshToken);
                Request req = request("/auth/v1/token?grant_type=refresh_token")
                        .header("apikey", BuildConfig.SUPABASE_KEY)
                        .header("Authorization", "Bearer " + BuildConfig.SUPABASE_KEY)
                        .post(json(body))
                        .build();
                try (Response res = sPlain.newCall(req).execute()) {
                    String resBody = res.body().string();
                    if (res.isSuccessful()) {
                        prefs(context).edit().putString(KEY_AUTH, resBody).commit();
                        return new JSONObject(resBody).optString("access_token", null);
                    }
                    Log.w(TAG, "Token refresh rejected: " + res.code());
                    // Only a definitive rejection logs out; 5xx keeps the session for a later retry.
                    if (res.code() >= 400 && res.code() < 500) clearSession(context);
                }
            } catch (IOException | JSONException e) {
                Log.w(TAG, "Token refresh failed", e);
            }
            return null;
        }
    }

    public static void clearSession(Context context) {
        prefs(context).edit().remove(KEY_AUTH).apply();
    }

    /** Clears the session and opens the login screen. Safe from any thread. */
    public static void redirectToLogin(@Nullable Activity activity) {
        if (activity == null) return;
        clearSession(activity);
        activity.runOnUiThread(() -> {
            Intent intent = new Intent(activity, LoginActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            activity.startActivity(intent);
        });
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }
}
