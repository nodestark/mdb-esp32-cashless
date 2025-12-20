package xyz.vmflow.target;

import android.Manifest;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;

import com.google.android.material.bottomnavigation.BottomNavigationView;

import xyz.vmflow.R;

public class MainActivity extends AppCompatActivity {

    public boolean onOptionsItemSelected(MenuItem item) {

        if (item.getItemId() == R.id.action_about) {

            String version = "";
            try {
                version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            } catch (Exception e) {
            }

            new AlertDialog.Builder(this)
                    .setTitle("About")
                    .setMessage(version + "\n\nThis app is part of an open source project implementing the MDB protocol for vending machines, focused on cashless payments and remote telemetry.\n\n" +
                            "Key features:\n\n" +
                            "Communication with vending machines via MDB\n\n" +
                            "Integration of payments via Bluetooth and MQTT\n\n" +
                            "Remote monitoring and real-time telemetry\n\n" +
                            "Data storage and management using Supabase\n\n" +
                            "Platform based on ESP32, with PCB design in KiCad\n\n" +
                            "Support for EVA DTS/DDCMP reading\n\n" +
                            "Contributions welcome: pull requests, suggestions for new features, and documentation improvements.")
                    .setNeutralButton("GitHub", (dialog, which) -> {
                        startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/nodestark/mdb-esp32-cashless")));
                    })
                    .setPositiveButton("Nice!", (dialog, which) -> dialog.dismiss())
                    .show();
            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar( toolbar );
        getSupportActionBar().setTitle( getString(R.string.app_name) );

        BottomNavigationView bottomNav = findViewById(R.id.bottom_navigation);
        bottomNav.setOnItemSelectedListener(item -> {

            Fragment selectedFragment = null;

            if(item.getItemId() == R.id.nav_embeddeds )
                selectedFragment = new EmbeddesFragment();

            if(item.getItemId() == R.id.nav_nearest )
                selectedFragment = new NearestFragment();

            if(item.getItemId() == R.id.nav_sales )
                selectedFragment = new SalesFragment();

            getSupportFragmentManager()
                    .beginTransaction()
                    .replace(R.id.container, selectedFragment)
                    .commit();

            return true;
        });

        bottomNav.setSelectedItemId(R.id.nav_sales);
    }

    @Override
    protected void onStart() {
        super.onStart();

        if (hasBluetoothPermissions()) {
            checkAndEnableBluetooth();
        } else {
            requestBluetoothPermissions();
        }

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions( new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, 102 );
        }
    }

    private boolean hasBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
                    && ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
        } else {
            return ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH) == PackageManager.PERMISSION_GRANTED
                    && ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_ADMIN) == PackageManager.PERMISSION_GRANTED
                    && ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
        }
    }

    private void requestBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT}, 101);
        } else {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.BLUETOOTH, Manifest.permission.BLUETOOTH_ADMIN, Manifest.permission.ACCESS_FINE_LOCATION}, 101);
        }
    }

    private void checkAndEnableBluetooth() {
        BluetoothAdapter adapter = getSystemService(BluetoothManager.class).getAdapter();
        if (adapter != null && !adapter.isEnabled()) {
            adapter.enable();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == 101 && hasBluetoothPermissions()) {
            checkAndEnableBluetooth();
        }
    }
}
