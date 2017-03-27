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
import android.support.v4.view.GravityCompat;
import android.support.v4.widget.DrawerLayout;
import android.support.v7.app.AppCompatActivity;
import android.view.MenuItem;
import android.view.View;
import android.webkit.WebView;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.Toast;
import com.android.volley.Request;
import com.android.volley.RequestQueue;
import com.android.volley.Response;
import com.android.volley.VolleyError;
import com.android.volley.toolbox.StringRequest;
import com.android.volley.toolbox.Volley;
import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.text.SimpleDateFormat;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.util.Arrays;
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
    FloatingActionButton saveFlightData;
    FloatingActionButton flightGraphsButton;
    private ListView appDrawerList;
    private ArrayAdapter<String> appDrawListAdapter;
    private DrawerLayout appDrawerLayout;
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
                    myWebView.loadUrl("file:///android_asset/html/graphs.html");
                    connectedToTimer = false;
                    saveFlightData.hide();
                    flightGraphsButton.hide();
                }
            } else {
                switch (monitorState) {
                    case graph:
                        makeTelemetryRequest();
                        saveFlightData.show();
                        break;
                    case settings:
                        myWebView.loadUrl("http://192.168.1.1/telemetry");
                        saveFlightData.hide();
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

    private void updateGraphWithJson(String jsonDataString) {
        try {
            final JSONObject jsonObject = new JSONObject(jsonDataString);
            final JSONArray escHistory = (JSONArray) jsonObject.get("escHistory");
            final JSONArray dtHistory = (JSONArray) jsonObject.get("dtHistory");
//            for (int index = 0; index < escHistory.length(); index++) {
//                escHistory.put((int) escHistory.get(index) / 10);
//                dtHistory.put((int) dtHistory.get(index) / 10);
//            }
            myWebView.loadUrl("javascript:flightChart.data.datasets[0].data = " + jsonObject.get("altitudeHistory").toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[1].data = " + jsonObject.get("temperatureHistory").toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[2].data = " + escHistory.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[3].data = " + dtHistory.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.update();");
            synchronized (jsonDataLock) {
                sharedJsonData = jsonDataString;
            }
        } catch (JSONException e) {
            myWebView.loadUrl("javascript:document.write(\"" + e.getMessage() + "\");");
        }
    }

    private void makeTelemetryRequest() {
        StringRequest telemetryRequest = new StringRequest(Request.Method.GET, "http://192.168.1.1/graph.json", //"file:///android_asset/html/telemetry.json",
                new Response.Listener<String>() {
            @Override
            public void onResponse(String response) {
                updateGraphWithJson(response);
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
        appDrawerList = (ListView) findViewById(R.id.savedFlightsList);
        appDrawListAdapter = new ArrayAdapter<String>(this, android.R.layout.simple_list_item_1);
        appDrawerList.setAdapter(appDrawListAdapter);
        requestQueue = Volley.newRequestQueue(getApplicationContext());
        myWebView = (WebView) findViewById(R.id.webview);
        myWebView.getSettings().setJavaScriptEnabled(true);
        myWebView.loadUrl("file:///android_asset/html/graphs.html");
        saveFlightData = (FloatingActionButton) findViewById(R.id.saveFlightData);
        flightGraphsButton = (FloatingActionButton) findViewById(R.id.flightGraphs);
        flightGraphsButton.hide();
        saveFlightData.hide();
        appDrawerList.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                try {
                    if (!myWebView.getUrl().equals("file:///android_asset/html/graphs.html")) {
                        myWebView.loadUrl("file:///android_asset/html/graphs.html");
                    }
                    FileInputStream inputStream = openFileInput(appDrawListAdapter.getItem(position));
                    InputStreamReader inputStreamReader = new InputStreamReader(inputStream);
                    BufferedReader bufferedReader = new BufferedReader(inputStreamReader);
                    StringBuilder stringBuilder = new StringBuilder();
                    String line;
                    while ((line = bufferedReader.readLine()) != null) {
                        stringBuilder.append(line);
                    }
                    updateGraphWithJson(stringBuilder.toString());
                    Toast.makeText(MainActivity.this, appDrawListAdapter.getItem(position), Toast.LENGTH_SHORT).show();
                } catch (IOException e) {
                    Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_LONG).show();
                }
                appDrawerLayout.closeDrawers();
            }
        });
        appDrawerLayout = (DrawerLayout) findViewById(R.id.drawer_layout);
        getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        getSupportActionBar().setHomeButtonEnabled(true);
        appDrawerLayout.addDrawerListener(new DrawerLayout.DrawerListener() {

            @Override
            public void onDrawerSlide(View drawerView, float slideOffset) {

            }

            @Override
            public void onDrawerOpened(View drawerView) {
                final String[] fileNameArray = fileList();
                Arrays.sort(fileNameArray);
                appDrawListAdapter.clear();
                appDrawListAdapter.addAll(Arrays.asList(fileNameArray));
            }

            @Override
            public void onDrawerClosed(View drawerView) {

            }

            @Override
            public void onDrawerStateChanged(int newState) {

            }
        });
        saveFlightData.setOnClickListener(new View.OnClickListener() {
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
        flightGraphsButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                monitorState = MonitorState.graph;
                //myWebView.loadUrl("http://192.168.1.1/graph.json");
                myWebView.loadUrl("file:///android_asset/html/graphs.html");
                saveFlightData.show();
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
                saveFlightData.hide();
                myWebView.loadUrl("http://192.168.1.1/telemetry");
                Snackbar.make(view, wifiMessage, Snackbar.LENGTH_LONG).setAction("Action", null).show();
            }
        });
        connectionCheckHandler.postDelayed(connectionCheckRunnable, 0);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case android.R.id.home:
                if (appDrawerLayout.isDrawerOpen(GravityCompat.START)) {
                    appDrawerLayout.closeDrawers();
                } else {
                    appDrawerLayout.openDrawer(GravityCompat.START);
                }
                return true;
        }
        return super.onOptionsItemSelected(item);
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