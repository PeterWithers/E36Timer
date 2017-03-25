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
import org.json.JSONException;
import org.json.JSONObject;
import java.util.List;

/**
 * @since March 24, 2016 21:51:11 PM (creation date)
 * @author : Peter Withers <peter@gthb-bambooradical.com>
 */
public class MainActivity extends AppCompatActivity {

    boolean connectedToTimer = false;
    FloatingActionButton connectionButton;
    WebView myWebView;
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
                }
            } else if (!connectedToTimer) {
                //myWebView.loadUrl("http://192.168.1.1/telemetry");
                connectedToTimer = true;
            }
            connectionCheckHandler.postDelayed(this, 500);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        requestQueue = Volley.newRequestQueue(getApplicationContext());
        myWebView = (WebView) findViewById(R.id.webview);
        myWebView.getSettings().setJavaScriptEnabled(true);
        final FloatingActionButton addFlightTest = (FloatingActionButton) findViewById(R.id.addFlightTest);
//        addFlightTest.setEnabled(false);
        addFlightTest.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                StringRequest telemetryRequest = new StringRequest(Request.Method.GET, "http://192.168.1.1/graph.json", //"file:///android_asset/html/telemetry.json",
                        new Response.Listener<String>() {
                    @Override
                    public void onResponse(String response) {
                        try {
                            final JSONObject jsonObject = new JSONObject(response);
                            myWebView.loadUrl("javascript:flightChart.data.datasets[0].data = " + jsonObject.get("altitudeHistory").toString() + ";");
                            myWebView.loadUrl("javascript:flightChart.data.datasets[1].data = " + jsonObject.get("temperatureHistory").toString() + ";");
                            myWebView.loadUrl("javascript:flightChart.update();");
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
        });

        final FloatingActionButton flightGraphsButton = (FloatingActionButton) findViewById(R.id.flightGraphs);
//        flightGraphsButton.setEnabled(false);
        flightGraphsButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
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
