package xyz.vmflow;

import android.bluetooth.BluetoothDevice;
import android.bluetooth.le.ScanResult;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.util.List;

public class ItemAdapter_ extends RecyclerView.Adapter<ItemAdapter_.ViewHolder_> {

    private List<ScanResult> scanResults;
    private ItemAdapter_OnItemClickListener listener;

    public ItemAdapter_(List<ScanResult> scanResults, ItemAdapter_OnItemClickListener listener) {
        this.scanResults = scanResults;
        this.listener = listener;
    }

    public ViewHolder_ onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {

        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_layout, parent, false);

        return new ViewHolder_(view);
    }

    public void onBindViewHolder(@NonNull ViewHolder_ holder, int position) {

        ScanResult scanResult = scanResults.get(position);

        BluetoothDevice device = scanResult.getDevice();
        holder.deviceNameText.setText( "\uD83C\uDF6B Machine: " + device.getName().substring(7) );
        holder.deviceAddressText.setText(device.getAddress());

        holder.bind(scanResult, listener);
    }

    public int getItemCount() {
        return scanResults.size();
    }

    public static class ViewHolder_ extends RecyclerView.ViewHolder {
        public TextView deviceNameText;
        public TextView deviceAddressText;

        public ViewHolder_(View view) {
            super(view);

            deviceNameText = view.findViewById(R.id.textViewDeviceName);
            deviceAddressText = view.findViewById(R.id.textViewDeviceAddress);
        }

        public void bind(ScanResult result, ItemAdapter_OnItemClickListener listener) {
            itemView.setOnClickListener(v -> listener.onItemClick(result));
        }
    }
}
