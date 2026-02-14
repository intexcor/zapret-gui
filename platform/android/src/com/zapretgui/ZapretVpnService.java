package com.zapretgui;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.net.VpnService;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.File;
import java.io.IOException;

public class ZapretVpnService extends VpnService {
    private static final String TAG = "ZapretVpnService";
    private static final String CHANNEL_ID = "zapret_vpn";
    private static final int NOTIFICATION_ID = 1;

    private ParcelFileDescriptor mTunFd;
    private Process mTpwsProcess;
    private static ZapretVpnService sInstance;

    @Override
    public void onCreate() {
        super.onCreate();
        sInstance = this;
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && "STOP".equals(intent.getAction())) {
            stopVpn();
            stopSelf();
            return START_NOT_STICKY;
        }

        startForeground(NOTIFICATION_ID, buildNotification());
        startVpn();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopVpn();
        sInstance = null;
        super.onDestroy();
    }

    private void startVpn() {
        try {
            // Create TUN interface
            Builder builder = new Builder();
            builder.setSession("Zapret DPI Bypass");
            builder.addAddress("10.120.0.1", 30);
            builder.addRoute("0.0.0.0", 0);
            builder.addDnsServer("1.1.1.1");
            builder.addDnsServer("8.8.8.8");
            builder.setMtu(1500);

            // Exclude our own app from VPN to avoid loops
            try {
                builder.addDisallowedApplication(getPackageName());
            } catch (Exception e) {
                Log.w(TAG, "Failed to exclude self from VPN", e);
            }

            mTunFd = builder.establish();
            if (mTunFd == null) {
                Log.e(TAG, "Failed to establish VPN tunnel");
                return;
            }

            // Start tpws as SOCKS proxy
            startTpws();

            Log.i(TAG, "VPN started successfully");
        } catch (Exception e) {
            Log.e(TAG, "Failed to start VPN", e);
            stopVpn();
        }
    }

    private void startTpws() {
        try {
            String nativeLibDir = getApplicationInfo().nativeLibraryDir;
            String tpwsPath = nativeLibDir + "/libtpws.so";

            File tpwsBinary = new File(tpwsPath);
            if (!tpwsBinary.exists()) {
                Log.e(TAG, "tpws binary not found at: " + tpwsPath);
                return;
            }

            // Ensure executable
            tpwsBinary.setExecutable(true);

            String dataDir = getFilesDir().getAbsolutePath();

            ProcessBuilder pb = new ProcessBuilder(
                tpwsPath,
                "--port", "1080",
                "--bind-addr=127.0.0.1",
                "--hostlist=" + dataDir + "/list-general.txt",
                "--split-pos=1"
            );

            pb.redirectErrorStream(true);
            mTpwsProcess = pb.start();
            Log.i(TAG, "tpws started with PID: " + mTpwsProcess);
        } catch (IOException e) {
            Log.e(TAG, "Failed to start tpws", e);
        }
    }

    private void stopVpn() {
        // Stop tpws
        if (mTpwsProcess != null) {
            mTpwsProcess.destroy();
            mTpwsProcess = null;
        }

        // Close TUN
        if (mTunFd != null) {
            try {
                mTunFd.close();
            } catch (IOException e) {
                Log.w(TAG, "Error closing TUN fd", e);
            }
            mTunFd = null;
        }
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Zapret VPN Service",
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("DPI bypass VPN is active");

            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.createNotificationChannel(channel);
            }
        }
    }

    private Notification buildNotification() {
        Intent stopIntent = new Intent(this, ZapretVpnService.class);
        stopIntent.setAction("STOP");
        PendingIntent stopPending = PendingIntent.getService(
            this, 0, stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE
        );

        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        return builder
            .setContentTitle("Zapret")
            .setContentText("DPI bypass is active")
            .setSmallIcon(android.R.drawable.ic_lock_lock)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Stop", stopPending)
            .setOngoing(true)
            .build();
    }

    // Static methods called from C++ via JNI
    public static void prepare(Context context) {
        Intent intent = VpnService.prepare(context);
        if (intent != null) {
            // Need to request VPN permission from user
            context.startActivity(intent);
        }
    }

    public static void start(Context context) {
        Intent intent = new Intent(context, ZapretVpnService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void stop() {
        if (sInstance != null) {
            sInstance.stopVpn();
            sInstance.stopSelf();
        }
    }
}
