package xyz.vmflow;

import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothProfile;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import java.util.UUID;

public class VendFlowActivity extends AppCompatActivity {

    private static final UUID SERVICE_UUID = UUID.fromString("b2bbc642-46da-11ed-b878-0242ac120002");
    private static final UUID WRITE_CHARACTERISTIC_UUID = UUID.fromString("c9af9c76-46de-11ed-b878-0242ac120002");

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {

        private static final byte VEND_REQUEST = 'a';
        private static final byte VEND_SUCCESS = 'b';
        private static final byte VEND_FAILURE = 'c';
        private static final byte SESSION_COMPLETE = 'd';
        private static final byte SESSION_BEGIN_TODO = 'e';
        private static final byte VEND_APPROVED_TODO = 'f';

        @Override
        public void onCharacteristicChanged(@NonNull BluetoothGatt gatt, @NonNull BluetoothGattCharacteristic characteristic, @NonNull byte[] value) {

            byte[] dados = characteristic.getValue();

            switch (dados[0]){
                case VEND_REQUEST: {

                    short itemPrice = (short) ((dados[1] << 8) | dados[2]);
                    short itemNumber = (short) ((dados[3] << 8) | dados[4]);
                    short nonce = (short) ((dados[5] << 8) | dados[6]);

                    BluetoothGattService service = gatt.getService( SERVICE_UUID );
                    BluetoothGattCharacteristic writeChar = service.getCharacteristic( WRITE_CHARACTERISTIC_UUID );

                    byte[] dados_ = new byte[]{VEND_APPROVED_TODO, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                    writeChar.setValue(dados_);

                    gatt.writeCharacteristic(writeChar);

                    Log.d("MDB-", "🔹 VEND_REQUEST Item price: " + itemPrice + ", Item number: " + itemNumber);
                }
                    break;
                case VEND_SUCCESS: {

                    short itemPrice = (short) ((dados[1] << 8) | dados[2]);
                    short itemNumber = (short) ((dados[3] << 8) | dados[4]);
                    short nonce = (short) ((dados[5] << 8) | dados[6]);

                    Log.d("MDB-", "🔹 VEND_SUCCESS Item price: " + itemPrice + ", Item number: " + itemNumber);
                }
                    break;
                case VEND_FAILURE:{
                    short itemPrice = (short) ((dados[1] << 8) | dados[2]);
                    short itemNumber = (short) ((dados[3] << 8) | dados[4]);
                    short nonce = (short) ((dados[5] << 8) | dados[6]);

                    Log.d("MDB-", "🔹 VEND_FAILURE Item price: " + itemPrice + ", Item number: " + itemNumber);
                }
                    break;
                case SESSION_COMPLETE: {

                    short itemPrice = (short) ((dados[1] << 8) | dados[2]);
                    short itemNumber = (short) ((dados[3] << 8) | dados[4]);
                    short nonce = (short) ((dados[5] << 8) | dados[6]);

                    finish();

                    Log.d("MDB-", "🔹 SESSION_COMPLETE Item price: " + itemPrice + ", Item number: " + itemNumber);
                }
                    break;
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

            BluetoothGattService service = gatt.getService( SERVICE_UUID );
            BluetoothGattCharacteristic writeChar = service.getCharacteristic( WRITE_CHARACTERISTIC_UUID );

            byte[] dados = new byte[]{SESSION_BEGIN_TODO, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            writeChar.setValue(dados);

            boolean ok = gatt.writeCharacteristic(writeChar);

            Log.d("MDB-", "Escrevendo characteristic: " + ok);
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {

            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d("MDB-", "Serviços descobertos" );

                BluetoothGattService service = gatt.getService( SERVICE_UUID );

                if (service != null) {
                    BluetoothGattCharacteristic writeChar = service.getCharacteristic( WRITE_CHARACTERISTIC_UUID );

                    if (writeChar != null) {

                        gatt.setCharacteristicNotification(writeChar, true);

                        BluetoothGattDescriptor descriptor = writeChar.getDescriptor( UUID.fromString("00002902-0000-1000-8000-00805f9b34fb") /*Default client UUID*/ );

                        if (descriptor != null) {
                            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                            gatt.writeDescriptor(descriptor);
                        }
                    }
                }
            } else {
                Log.d("MDB-", "Falha ao descobrir serviços, status: " + status);
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

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        BluetoothDevice bleDevice = getIntent().getParcelableExtra("device");

        runOnUiThread(() -> {
            Toast.makeText(getApplicationContext(), "Machine: " + bleDevice.getName().substring(7), Toast.LENGTH_LONG).show();
        });

        bluetoothGatt = bleDevice.connectGatt(getApplicationContext(), false, gattCallback);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        if( bluetoothGatt != null )
            bluetoothGatt.close();
    }
}