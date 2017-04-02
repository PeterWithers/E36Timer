/*
 * Copyright (C) 2016 Peter Withers
 */
package com.bambooradical.e36monitor;

import android.Manifest;
import android.annotation.TargetApi;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.NetworkRequest;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.support.design.widget.FloatingActionButton;
import android.support.design.widget.Snackbar;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
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
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.URL;
import java.net.URLConnection;
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
        liveGraph,
        historyGraph
    }
    private Network timerNetwork = null;
    private MonitorState monitorState = MonitorState.settings;
//    boolean connectedToTimer = false;
    FloatingActionButton connectionButton;
    FloatingActionButton saveFlightData;
    FloatingActionButton flightGraphsButton;
    private ListView appDrawerList;
    private ArrayAdapter<String> appDrawListAdapter;
    private DrawerLayout appDrawerLayout;
    WebView myWebView;
    volatile String sharedJsonData = "";
//    volatile int currentJsonGraphIndex = 0;
    static final Object jsonDataLock = new Object();
//    final JSONArray altitudeHistoryRms = new JSONArray();
//    final JSONArray altitudeHistorySmoothed = new JSONArray();
//    final JSONArray escHistoryFull = new JSONArray();
//    final JSONArray dtHistoryFull = new JSONArray();
//    final JSONArray altitudeHistoryFull = new JSONArray();
//    final JSONArray temperatureHistoryFull = new JSONArray();
    RequestQueue requestQueue;
    Handler connectionCheckHandler = new Handler();
    Runnable connectionCheckRunnable = new Runnable() {

        @Override
        public void run() {
            WifiManager wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            WifiInfo wifiInfo = wifiManager.getConnectionInfo();
            if (wifiInfo.getSSID() == null || !wifiInfo.getSSID().equals("\"E36 Timer\"")) {
//                if (connectedToTimer) {
                //myWebView.loadUrl("file:///android_asset/html/graphs.html");
//                    connectedToTimer = false;
                saveFlightData.hide();
                flightGraphsButton.hide();
//                }
            } else {
//                    case graph:
                makeTelemetryRequest();
//                        break;
//                    case settings:
//                        myWebView.loadUrl("http://192.168.1.1/telemetry");
//                        saveFlightData.hide();
//                        break;
//                }
//                if (!connectedToTimer) {
//                    connectedToTimer = true;
//                }
                //flightGraphsButton.show();
            }
            connectionCheckHandler.postDelayed(this, 2000);
        }
    };

    private void updateGraphWithJson(String jsonDataString) {
        try {
            final JSONObject jsonObject = new JSONObject(jsonDataString);
            final JSONArray escHistory = (JSONArray) jsonObject.get("escHistory");
            final JSONArray dtHistory = (JSONArray) jsonObject.get("dtHistory");
            final JSONArray altitudeHistory = (JSONArray) jsonObject.get("altitudeHistory");
            final JSONArray temperatureHistory = (JSONArray) jsonObject.get("temperatureHistory");
            double value0 = 0;
            double value1 = 0;
            double value2 = 0;
            double value3 = 0;
            double value4 = 0;
            double value5 = 0;
            final JSONArray altitudeHistoryRms = new JSONArray();
            final JSONArray altitudeHistorySmoothed = new JSONArray();
            //int dataLength = 0;
//            int startIndex = (int) jsonObject.get("startIndex");
            int totalLength = (int) jsonObject.get("historyIndex");
            try {
                for (int index = 0; index < altitudeHistory.length(); index++) {
                    final Double value = altitudeHistory.getDouble(index);
                    //currentJsonGraphIndex++;
                    //dataLength++;
                    value5 = value4;
                    value4 = value3;
                    value3 = value2;
                    value2 = value1;
                    value1 = value0;
                    value0 = value;
                    altitudeHistoryRms.put(Math.sqrt((value5 * value5 + value4 * value4 + value3 * value3 + value2 * value2 + value1 * value1 + value0 * value0) / 6));
                    altitudeHistorySmoothed.put((value5 * 0.1 + value4 * 0.1 + value3 * 0.2 + value2 * 0.3 + value1 * 0.2 + value0 * 0.1));
//                    escHistoryFull.put(startIndex + index, escHistory.getInt(index));
//                    dtHistoryFull.put(startIndex + index, dtHistory.getInt(index));
//                    altitudeHistoryFull.put(startIndex + index, altitudeHistory.getDouble(index));
//                    temperatureHistoryFull.put(startIndex + index, temperatureHistory.getDouble(index));
                }
            } catch (JSONException e) {
//                Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_SHORT).show();
            }
            //.splice(" + startIndex + "," + dataLength + "," + 
//            currentJsonGraphIndex = altitudeHistoryFull.length();
            myWebView.loadUrl("javascript:flightChart.data.labels = new Array(" + totalLength + ");");
            myWebView.loadUrl("javascript:flightChart.data.datasets[0].data = " + altitudeHistory.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[1].data = " + altitudeHistoryRms.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[2].data = " + altitudeHistorySmoothed.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[3].data = " + temperatureHistory.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[4].data = " + escHistory.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[5].data = " + dtHistory.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.update();");
            synchronized (jsonDataLock) {
                sharedJsonData = jsonDataString;
            }
        } catch (JSONException e) {
            Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

//    @TargetApi(21)
    private void makeTelemetryRequest() {
//        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
//            if (timerNetwork != null) {
//                try {
//                    InputStreamReader inputStreamReader;
//                    URLConnection urlConnection = timerNetwork.openConnection(new URL("http://192.168.1.1/graph.json"));
//                    inputStreamReader = new InputStreamReader(urlConnection.getInputStream());
//
//                    BufferedReader bufferedReader = new BufferedReader(inputStreamReader);
//                    StringBuilder stringBuilder = new StringBuilder();
//                    String line;
//                    while ((line = bufferedReader.readLine()) != null) {
//                        stringBuilder.append(line);
//                    }
//                    if (monitorState = MonitorState.liveGraph) {
//                        updateGraphWithJson(stringBuilder.toString());
//                        saveFlightData.show();
//                    } else {
//                      saveFlightData.hide();
//                    }
//                } catch (IOException e) {
//                    Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_LONG).show();
//                    saveFlightData.hide();
//                }
//            } else {
//            //    Toast.makeText(MainActivity.this, "timerNetwork is null", Toast.LENGTH_LONG).show();
//                  saveFlightData.hide();
//            }
//        } else {
        StringRequest telemetryRequest = new StringRequest(Request.Method.GET, "http://192.168.1.1/graph.json", //?start=" + currentJsonGraphIndex, //"file:///android_asset/html/telemetry.json",
                new Response.Listener<String>() {
            @Override
            public void onResponse(String response) {
                if (monitorState == MonitorState.liveGraph) {
                    updateGraphWithJson(response);
                    if (monitorState == MonitorState.liveGraph) {
                        saveFlightData.show();
                    } else {
                        saveFlightData.hide();
                    }
                }
            }
        }, new Response.ErrorListener() {
            @Override
            public void onErrorResponse(VolleyError error) {
                Toast.makeText(MainActivity.this, error.getMessage(), Toast.LENGTH_LONG).show();
                saveFlightData.hide();
            }
        });
        requestQueue.add(telemetryRequest);
//        }
    }

//    @TargetApi(21)
    private void bindToNetwork(boolean bind) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            if (PackageManager.PERMISSION_GRANTED != ContextCompat.checkSelfPermission(getApplicationContext(), Manifest.permission.CHANGE_NETWORK_STATE)
                    || PackageManager.PERMISSION_GRANTED != ContextCompat.checkSelfPermission(getApplicationContext(), Manifest.permission.WRITE_SETTINGS)) {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.WRITE_SETTINGS, Manifest.permission.CHANGE_NETWORK_STATE}, 123);
            } else {
                final WifiManager wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                final ConnectivityManager connectivityManager = (ConnectivityManager) getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
                NetworkRequest.Builder req = new NetworkRequest.Builder();
                req.addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
                req.addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
                connectivityManager.requestNetwork(req.build(), new ConnectivityManager.NetworkCallback() {
                    @Override
                    public void onAvailable(Network network) {
                        //here you can use bindProcessToNetwork
                        //String ssid = wifiManager.getConnectionInfo().getSSID();
                        //if ("\"E36 Timer\"".equals(ssid)) {
//                        connectivityManager.bindProcessToNetwork(network);
                        timerNetwork = network;
//                            connectedToTimer = true;
                        //} else {
                        //    timerNetwork = null;
                        //    connectedToTimer = false;
                        //}
                    }
                });
            }
        }
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
                    monitorState = MonitorState.historyGraph;
//                    if (!myWebView.getUrl().equals("file:///android_asset/html/graphs.html")) {
//                        myWebView.loadUrl("file:///android_asset/html/graphs.html");
//                    }
                    String selectedValue = appDrawListAdapter.getItem(position);
                    InputStream inputStream;
                    if ("demo data 1".equals(selectedValue)) {
                        inputStream = getAssets().open("html/telemetry.json");
                    } else if ("demo data 2".equals(selectedValue)) {
                        inputStream = getAssets().open("html/telemetry_1.json");
                    } else if ("demo data 3".equals(selectedValue)) {
                        inputStream = getAssets().open("html/telemetry_2.json");
                    } else {
                        inputStream = openFileInput(appDrawListAdapter.getItem(position));
                    }
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
                appDrawListAdapter.add("demo data 1");
                appDrawListAdapter.add("demo data 2");
                appDrawListAdapter.add("demo data 3");
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
                } catch (IOException e) {
                    Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_SHORT).show();
                }
            }
        });
        flightGraphsButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                monitorState = MonitorState.liveGraph;
                //myWebView.loadUrl("http://192.168.1.1/graph.json");
                //myWebView.loadUrl("file:///android_asset/html/graphs.html");
                //saveFlightData.show();
                Snackbar.make(view, "Flight Graphs", Snackbar.LENGTH_LONG).setAction("Action", null).show();
            }
        });
        connectionButton = (FloatingActionButton) findViewById(R.id.conectionInfo);
        connectionButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                monitorState = MonitorState.liveGraph;
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
                bindToNetwork(true);
                //saveFlightData.hide();
                //myWebView.loadUrl("http://192.168.1.1/telemetry");
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
