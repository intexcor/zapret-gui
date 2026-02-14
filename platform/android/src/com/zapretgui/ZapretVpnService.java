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
import java.io.FileInputStream;
import java.io.IOException;

public class ZapretVpnService extends VpnService {
    private static final String TAG = "ZapretVpnService";
    private static final String CHANNEL_ID = "zapret_vpn";
    private static final int NOTIFICATION_ID = 1;

    /* Intent extras for strategy configuration */
    public static final String EXTRA_FAKE_TTL = "fake_ttl";
    public static final String EXTRA_FAKE_REPEATS = "fake_repeats";
    public static final String EXTRA_FAKE_QUIC_PATH = "fake_quic_path";
    public static final String EXTRA_SPLIT_POS = "split_pos";
    public static final String EXTRA_USE_DISORDER = "use_disorder";

    private ParcelFileDescriptor mTunFd;
    private static ZapretVpnService sInstance;

    static {
        System.loadLibrary("vpn-processor");
    }

    /* Native methods implemented in vpn_processor.c */
    private native void nativeStart(int tunFd, byte[] fakePayload,
                                    int fakeTtl, int fakeRepeats,
                                    int splitPos, boolean useDisorder);
    private native void nativeStop();

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

        /* Extract strategy parameters from intent */
        int fakeTtl = 3;
        int fakeRepeats = 6;
        String fakeQuicPath = null;
        int splitPos = 1;
        boolean useDisorder = false;

        if (intent != null) {
            fakeTtl = intent.getIntExtra(EXTRA_FAKE_TTL, 3);
            fakeRepeats = intent.getIntExtra(EXTRA_FAKE_REPEATS, 6);
            fakeQuicPath = intent.getStringExtra(EXTRA_FAKE_QUIC_PATH);
            splitPos = intent.getIntExtra(EXTRA_SPLIT_POS, 1);
            useDisorder = intent.getBooleanExtra(EXTRA_USE_DISORDER, false);
        }

        startVpn(fakeTtl, fakeRepeats, fakeQuicPath, splitPos, useDisorder);
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopVpn();
        sInstance = null;
        super.onDestroy();
    }

    private void startVpn(int fakeTtl, int fakeRepeats, String fakeQuicPath,
                          int splitPos, boolean useDisorder) {
        try {
            /* Create TUN interface */
            Builder builder = new Builder();
            builder.setSession("Zapret DPI Bypass");
            builder.addAddress("10.120.0.1", 30);
            builder.addRoute("0.0.0.0", 0);
            builder.addDnsServer("1.1.1.1");
            builder.addDnsServer("8.8.8.8");
            builder.setMtu(1500);

            /* Exclude our own app from VPN to avoid loops */
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

            /* Load fake QUIC payload from file */
            byte[] fakePayload = null;
            if (fakeQuicPath != null && !fakeQuicPath.isEmpty()) {
                fakePayload = loadFakePayload(fakeQuicPath);
            }

            /* Start native packet processor in background thread */
            nativeStart(mTunFd.getFd(), fakePayload,
                       fakeTtl, fakeRepeats, splitPos, useDisorder);

            Log.i(TAG, "VPN started: split=" + splitPos + " disorder=" + useDisorder
                    + " fakeTtl=" + fakeTtl + " fakeRepeats=" + fakeRepeats);
        } catch (Exception e) {
            Log.e(TAG, "Failed to start VPN", e);
            stopVpn();
        }
    }

    private byte[] loadFakePayload(String path) {
        try {
            File file = new File(path);
            if (!file.exists() || file.length() == 0 || file.length() > 4096) {
                Log.w(TAG, "Invalid fake payload: " + path);
                return null;
            }
            byte[] data = new byte[(int) file.length()];
            try (FileInputStream fis = new FileInputStream(file)) {
                int read = fis.read(data);
                if (read != data.length) {
                    Log.w(TAG, "Short read on fake payload: " + read + "/" + data.length);
                    return null;
                }
            }
            Log.i(TAG, "Loaded fake payload: " + path + " (" + data.length + " bytes)");
            return data;
        } catch (IOException e) {
            Log.e(TAG, "Failed to load fake payload: " + path, e);
            return null;
        }
    }

    private void stopVpn() {
        /* Stop native processor (blocks until thread exits) */
        nativeStop();

        /* Close TUN */
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

    /* Static methods called from C++ via JNI */
    public static void prepare(Context context) {
        Intent intent = VpnService.prepare(context);
        if (intent != null) {
            context.startActivity(intent);
        }
    }

    public static void start(Context context, int fakeTtl, int fakeRepeats,
                             String fakeQuicPath, int splitPos, boolean useDisorder) {
        Intent intent = new Intent(context, ZapretVpnService.class);
        intent.putExtra(EXTRA_FAKE_TTL, fakeTtl);
        intent.putExtra(EXTRA_FAKE_REPEATS, fakeRepeats);
        intent.putExtra(EXTRA_FAKE_QUIC_PATH, fakeQuicPath);
        intent.putExtra(EXTRA_SPLIT_POS, splitPos);
        intent.putExtra(EXTRA_USE_DISORDER, useDisorder);

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
