/*
 * Copyright (C) 2016 Peter Withers
 */
package com.bambooradical.e36monitor;

import android.content.Context;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.Handler;
import android.support.design.widget.FloatingActionButton;
import android.support.design.widget.Snackbar;
import android.support.v7.app.AppCompatActivity;
import android.view.View;
import android.webkit.WebView;
import com.android.volley.Request;
import com.android.volley.RequestQueue;
import com.android.volley.Response;
import com.android.volley.VolleyError;
import com.android.volley.toolbox.StringRequest;
import com.android.volley.toolbox.Volley;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.text.SimpleDateFormat;
import org.json.JSONException;
import org.json.JSONObject;
import java.util.Date;
import java.util.List;

/**
 * @since March 24, 2016 21:51:11 PM (creation date)
 * @author : Peter Withers <peter@gthb-bambooradical.com>
 */
public class MainActivity extends AppCompatActivity {

    enum MonitorState {
        settings,
        graph
    }
    MonitorState monitorState = MonitorState.settings;
    boolean connectedToTimer = false;
    FloatingActionButton connectionButton;
    FloatingActionButton addFlightTest;
    FloatingActionButton flightGraphsButton;
    WebView myWebView;
    volatile String sharedJsonData = "";
    static final Object jsonDataLock = new Object();
    RequestQueue requestQueue;
    Handler connectionCheckHandler = new Handler();
    Runnable connectionCheckRunnable = new Runnable() {

        @Override
        public void run() {
            WifiManager wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            WifiInfo wifiInfo = wifiManager.getConnectionInfo();
            if (wifiInfo.getSSID() == null || !wifiInfo.getSSID().equals("\"E36 Timer\"")) {
                if (connectedToTimer) {
                    //myWebView.loadUrl("file:///android_asset/html/index.html");
                    connectedToTimer = false;
                    addFlightTest.hide();
                    flightGraphsButton.hide();
                }
            } else {
                switch (monitorState) {
                    case graph:
                        makeTelemetryRequest();
                        addFlightTest.show();
                        break;
                    case settings:
                        myWebView.loadUrl("http://192.168.1.1/telemetry");
                        addFlightTest.hide();
                        break;
                }
                if (!connectedToTimer) {
                    connectedToTimer = true;
                }
                flightGraphsButton.show();
            }
            connectionCheckHandler.postDelayed(this, 2000);
        }
    };

    private void makeTelemetryRequest() {
        StringRequest telemetryRequest = new StringRequest(Request.Method.GET, "http://192.168.1.1/graph.json", //"file:///android_asset/html/telemetry.json",
                new Response.Listener<String>() {
            @Override
            public void onResponse(String response) {
                try {
                    final JSONObject jsonObject = new JSONObject(response);
                    myWebView.loadUrl("javascript:flightChart.data.datasets[0].data = " + jsonObject.get("altitudeHistory").toString() + ";");
                    myWebView.loadUrl("javascript:flightChart.data.datasets[1].data = " + jsonObject.get("temperatureHistory").toString() + ";");
                    myWebView.loadUrl("javascript:flightChart.data.datasets[2].data = " + jsonObject.get("escHistory").toString() + ";");
                    myWebView.loadUrl("javascript:flightChart.data.datasets[3].data = " + jsonObject.get("dtHistory").toString() + ";");
                    myWebView.loadUrl("javascript:flightChart.update();");
                    synchronized (jsonDataLock) {
                        sharedJsonData = response;
                    }
                } catch (JSONException e) {
                    myWebView.loadUrl("javascript:document.write(\"" + e.getMessage() + "\");");
                }
            }
        }, new Response.ErrorListener() {
            @Override
            public void onErrorResponse(VolleyError error) {
                myWebView.loadUrl("http://192.168.1.1/graph.json");
            }
        });
        requestQueue.add(telemetryRequest);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        requestQueue = Volley.newRequestQueue(getApplicationContext());
        myWebView = (WebView) findViewById(R.id.webview);
        myWebView.getSettings().setJavaScriptEnabled(true);
        addFlightTest = (FloatingActionButton) findViewById(R.id.addFlightTest);
//        addFlightTest.setEnabled(false);
        addFlightTest.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                try {
                    final SimpleDateFormat simpleDateFormat = new SimpleDateFormat("dd-MMM-yyyy hh:mm");
                    FileOutputStream fileOutputStream = openFileOutput(simpleDateFormat.format(new Date()), Context.MODE_PRIVATE);
                    synchronized (jsonDataLock) {
                        fileOutputStream.write(sharedJsonData.getBytes());
                    }
                    fileOutputStream.close();
                    for (String fileName : fileList()) {
                        myWebView.loadUrl("javascript:document.write(\"" + fileName + "<br/>\");");
                    }
                } catch (IOException e) {
                    myWebView.loadUrl("javascript:document.write(\"" + e.getMessage() + "\");");
                }
            }
        });

        flightGraphsButton = (FloatingActionButton) findViewById(R.id.flightGraphs);
//        flightGraphsButton.setEnabled(false);
        flightGraphsButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                monitorState = MonitorState.graph;
                //myWebView.loadUrl("http://192.168.1.1/graph.json");
                myWebView.loadUrl("file:///android_asset/html/graphs.html");
                addFlightTest.show();
                Snackbar.make(view, "Flight Graphs", Snackbar.LENGTH_LONG).setAction("Action", null).show();
            }
        });
        connectionButton = (FloatingActionButton) findViewById(R.id.conectionInfo);
        connectionButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                monitorState = MonitorState.settings;
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
                    //    myWebView.loadUrl("file:///android_asset/html/index.html");
                    //} else {
                    //myWebView.loadUrl("file:///android_asset/html/connected.html");
                    //    myWebView.loadUrl("http://192.168.1.1/graph.json");
                }
                addFlightTest.hide();
                myWebView.loadUrl("http://192.168.1.1/telemetry");
//                flightGraphsButton.setEnabled(true);
                Snackbar.make(view, wifiMessage, Snackbar.LENGTH_LONG).setAction("Action", null).show();
            }
        });
        connectionCheckHandler.postDelayed(connectionCheckRunnable, 0);
    }

    @Override
    public void onResume() {
        super.onResume();
        connectionCheckHandler.postDelayed(connectionCheckRunnable, 0);
    }

    @Override
    public void onPause() {
        super.onPause();
        connectionCheckHandler.removeCallbacks(connectionCheckRunnable);
    }
}
