/*
 * Copyright (C) 2016 Peter Withers
 */
package com.bambooradical.e36monitor;

import android.content.Context;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.support.design.widget.FloatingActionButton;
import android.support.design.widget.Snackbar;
import android.support.v7.app.AppCompatActivity;
import android.view.View;
import android.webkit.WebView;
import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.List;
import java.util.Random;

/**
 * @since March 24, 2016 21:51:11 PM (creation date)
 * @author : Peter Withers <peter@gthb-bambooradical.com>
 */
public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        final WebView myWebView = (WebView) findViewById(R.id.webview);
        myWebView.getSettings().setJavaScriptEnabled(true);
        final FloatingActionButton addFlightTest = (FloatingActionButton) findViewById(R.id.addFlightTest);
        addFlightTest.setEnabled(false);
        addFlightTest.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                myWebView.loadUrl("javascript:flightChart.data.datasets[0].data.splice(0, 1);flightChart.data.datasets[0].data.push(" + new Random().nextInt(10) + ");flightChart.update();");
            }
        });

        final FloatingActionButton flightGraphsButton = (FloatingActionButton) findViewById(R.id.flightGraphs);
        flightGraphsButton.setEnabled(false);
        flightGraphsButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                //myWebView.loadUrl("http://192.168.1.1/graphs");
                myWebView.loadUrl("file:///android_asset/html/graphs.html");
                addFlightTest.setEnabled(true);
                Snackbar.make(view, "Flight Graphs", Snackbar.LENGTH_LONG).setAction("Action", null).show();
            }
        });
        FloatingActionButton connectionButton = (FloatingActionButton) findViewById(R.id.conectionInfo);
        connectionButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                WifiManager wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                WifiInfo wifiInfo = wifiManager.getConnectionInfo();
                String wifiMessage = "Connected to: " + wifiInfo.getSSID();
                if (wifiInfo.getSSID() == null || !wifiInfo.getSSID().equals("\"E36 Timer\"")) {
                    List<WifiConfiguration> wifiList = wifiManager.getConfiguredNetworks();
                    if (wifiList != null) {
                        for (WifiConfiguration wifiConfiguration : wifiList) {
                            wifiMessage += "\nScanned: " + wifiConfiguration.SSID;
                            if (wifiConfiguration.SSID != null && wifiConfiguration.SSID.equals("\"E36 Timer\"")) {
                                wifiManager.disconnect();
                                wifiManager.enableNetwork(wifiConfiguration.networkId, true);
                                wifiManager.reconnect();
                                wifiMessage += "\nReconnecting";
                                break;
                            }
                        }
                    }
                    myWebView.loadUrl("file:///android_asset/html/index.html");
                } else {
                    myWebView.loadUrl("file:///android_asset/html/connected.html");
                }
                flightGraphsButton.setEnabled(true);
                Snackbar.make(view, wifiMessage, Snackbar.LENGTH_LONG).setAction("Action", null).show();
            }
        });
    }
}
