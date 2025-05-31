package com.example.myapplication;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends AppCompatActivity {

    private final List<ScanResult> scanResults = new ArrayList<>();
    private ItemAdapter_ adapter;
    private BluetoothLeScanner bluetoothLeScanner;
    private ScanCallback scanCallback;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        scanCallback = new ScanCallback() {

            public void onScanResult(int callbackType, ScanResult result) {

                BluetoothDevice device = result.getDevice();

                String deviceName = device.getName();
                if (deviceName == null || !deviceName.toLowerCase().startsWith("machine"))
                    return;

                boolean exists = false;

                for (ScanResult sr : scanResults) {
                    if (sr.getDevice().getAddress().equals(device.getAddress())) {
                        exists = true;
                        break;
                    }
                }

                if (!exists) {
                    scanResults.add(result);
                    adapter.notifyItemInserted(scanResults.size() - 1);
                }
            }

            @Override
            public void onScanFailed(int errorCode) {
                Log.e("MDB-", "Erro no scan: " + errorCode);
            }
        };

        adapter = new ItemAdapter_(scanResults, new ItemAdapter_OnItemClickListener() {
            @Override
            public void onItemClick(ScanResult device) {

                Intent intent = new Intent(MainActivity.this, VendFlowActivity.class);
                intent.putExtra("device", device.getDevice());
                startActivity(intent);
            }
        });

        BluetoothManager bluetoothManager = getSystemService(BluetoothManager.class);
        BluetoothAdapter bluetoothAdapter = bluetoothManager.getAdapter();
        if (bluetoothAdapter != null && !bluetoothAdapter.isEnabled() ) {

            ActivityResultLauncher<Intent> enableBluetoothLauncher = registerForActivityResult( new ActivityResultContracts.StartActivityForResult(), r -> {
                if (r.getResultCode() == Activity.RESULT_OK) { }
            } );
            enableBluetoothLauncher.launch( new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE) );
        }

        bluetoothLeScanner = bluetoothAdapter.getBluetoothLeScanner();

        RecyclerView recyclerView = findViewById(R.id.recyclerView);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));

        recyclerView.setAdapter(adapter);
    }

    @Override
    protected void onResume() {
        super.onResume();

        bluetoothLeScanner.startScan(scanCallback);
    }

    @Override
    protected void onPause() {
        super.onPause();

        bluetoothLeScanner.stopScan(scanCallback);
    }
}