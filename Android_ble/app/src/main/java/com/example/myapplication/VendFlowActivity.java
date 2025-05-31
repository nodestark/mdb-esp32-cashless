package com.example.myapplication;

import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothProfile;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import java.util.UUID;

public class VendFlowActivity extends AppCompatActivity {

    private BluetoothGattCharacteristic writeChar; // guardamos para usar depois

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {

        @Override
        public void onCharacteristicChanged(@NonNull BluetoothGatt gatt, @NonNull BluetoothGattCharacteristic characteristic, @NonNull byte[] value) {
            byte[] dados = characteristic.getValue();

            if(dados[0] == 'a'){
                // VEND_REQUEST

                short itemPrice = (short) ((dados[1] << 8) | dados[2]);
                short itemNumber = (short) ((dados[3] << 8) | dados[4]);
                short nonce = (short) ((dados[5] << 8) | dados[6]);

                Log.d("MDB-", "🔹 VEND_REQUEST Item price: " + itemPrice + ", Item number: " + itemNumber);

//                BluetoothGattService service = gatt.getService(UUID.fromString("b2bbc642-46da-11ed-b878-0242ac120002"));
//                BluetoothGattCharacteristic writeChar_ = service.getCharacteristic(UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002"));

                // Enviar dados
                byte[] dados_ = new byte[]{'f', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                writeChar.setValue(dados_);

                boolean ok = gatt.writeCharacteristic(writeChar);

                Log.d("MDB-", "Escrevendo characteristic: " + ok);
            } else if(dados[0] == 'b'){
                // VEND_SUCCESS

                short itemPrice = (short) ((dados[1] << 8) | dados[2]);
                short itemNumber = (short) ((dados[3] << 8) | dados[4]);
                short nonce = (short) ((dados[5] << 8) | dados[6]);

                Log.d("MDB-", "🔹 VEND_SUCCESS Item price: " + itemPrice + ", Item number: " + itemNumber);

            } else if(dados[0] == 'd'){
                // SESSION_COMPLETE

                short itemPrice = (short) ((dados[1] << 8) | dados[2]);
                short itemNumber = (short) ((dados[3] << 8) | dados[4]);
                short nonce = (short) ((dados[5] << 8) | dados[6]);

                Log.d("MDB-", "🔹 SESSION_COMPLETE Item price: " + itemPrice + ", Item number: " + itemNumber);

                finish();
            }
        }

        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {

            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d("MDB-", "Conectado");
                gatt.discoverServices(); // Buscar serviços

            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.d("MDB-", "Desconectado");
            }
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {

            // Enviar dados
            byte[] dados = new byte[]{'e', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            writeChar.setValue(dados);

            boolean ok = gatt.writeCharacteristic(writeChar);

            Log.d("MDB-", "Escrevendo characteristic: " + ok);
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {

            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d("MDB-", "Serviços descobertos" );

                // 🔍 Achar a characteristic que você quer
                BluetoothGattService service = gatt.getService(UUID.fromString("b2bbc642-46da-11ed-b878-0242ac120002"));

                if (service != null) {
                    writeChar = service.getCharacteristic(UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002"));

                    if (writeChar != null) {

                        gatt.setCharacteristicNotification(writeChar, true);

                        BluetoothGattDescriptor descriptor = writeChar.getDescriptor(
                                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb") // UUID padrão do Client Characteristic Configuration Descriptor
                        );

                        if (descriptor != null) {
                            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                            gatt.writeDescriptor(descriptor);
                        }
                    }
                }
            } else {
                Log.e("MDB-", "Falha ao descobrir serviços, status: " + status);
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            Log.d("MDB-", "Write status: " + status);
        }
    };

    private BluetoothGatt bluetoothGatt;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_vend_flow);

        BluetoothDevice device = getIntent().getParcelableExtra("device");

        Toast.makeText(getApplicationContext(), device.getName(), Toast.LENGTH_LONG).show();

        bluetoothGatt = device.connectGatt(getApplicationContext(), false, gattCallback);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        if( bluetoothGatt != null )
            bluetoothGatt.close();
    }
}