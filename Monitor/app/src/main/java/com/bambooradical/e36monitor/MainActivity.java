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
import android.os.AsyncTask;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
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
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.MalformedURLException;
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
    volatile int currentJsonGraphIndex = 0;
    static final Object jsonDataLock = new Object();
    private JSONArray altitudeHistoryRms = new JSONArray();
    private JSONArray altitudeHistorySmoothed = new JSONArray();
    private JSONArray escHistoryFull = new JSONArray();
    private JSONArray dtHistoryFull = new JSONArray();
    private JSONArray altitudeHistoryFull = new JSONArray();
    private JSONArray temperatureHistoryFull = new JSONArray();
    private RequestQueue requestQueue;
    private Handler connectionCheckHandler = new Handler();
    private AsyncTask connectionAsyncTask = null;

    private AsyncTask getAsyncTask() {
        return new AsyncTask<URL, Integer, String>() {
            protected String doInBackground(URL... url) {
                if (timerNetwork == null) {
//                WifiManager wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
//                WifiInfo wifiInfo = wifiManager.getConnectionInfo();
//                if (wifiInfo.getSSID() == null || !wifiInfo.getSSID().equals("\"E36 Timer\"")) {
                    final ConnectivityManager connectivityManager = (ConnectivityManager) getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
                    for (Network network : connectivityManager.getAllNetworks()) {
                        NetworkInfo networkInfo = connectivityManager.getNetworkInfo(network);
//                        System.out.println("network:" + network.toString());
//                        System.out.println("networkinfo:" + networkInfo.getTypeName());
//                        System.out.println("networkinfo:" + networkInfo.isConnected());
//                        System.out.println("networkinfo:" + networkInfo.getTypeName());
                        if (networkInfo.getType() == ConnectivityManager.TYPE_WIFI
                                && networkInfo.isConnected()
                                && "\"E36 Timer\"".equals(networkInfo.getExtraInfo())) {
                            timerNetwork = network;
                        }
                    }
//                }
                }
                if (timerNetwork != null) {
                    try {
                        InputStreamReader inputStreamReader;
                        URLConnection urlConnection = timerNetwork.openConnection(url[0]);
                        inputStreamReader = new InputStreamReader(urlConnection.getInputStream());

                        BufferedReader bufferedReader = new BufferedReader(inputStreamReader);
                        StringBuilder stringBuilder = new StringBuilder();
                        String line;
                        while ((line = bufferedReader.readLine()) != null) {
                            stringBuilder.append(line);
                        }
                        return stringBuilder.toString();
                    } catch (IOException e) {
                        timerNetwork = null;
                    }
                }
                return null;
            }

            protected void onProgressUpdate(Integer... progress) {
            }

            protected void onPostExecute(String result) {
                if (result == null || result.isEmpty()) {
                    Toast.makeText(MainActivity.this, (result == null) ? "network result null" : "network result empty", Toast.LENGTH_SHORT).show();
                    saveFlightData.hide();
                    flightGraphsButton.hide();
                } else if (monitorState == MonitorState.liveGraph) {
                    updateGraphWithJson(result);
                    saveFlightData.show();
                } else {
                    saveFlightData.hide();
                }
            }
        };
    }
    Runnable connectionCheckRunnable = new Runnable() {
        @Override
        public void run() {
            if (monitorState == MonitorState.liveGraph) {
                try {
                    if (connectionAsyncTask == null || connectionAsyncTask.getStatus() != AsyncTask.Status.RUNNING) {
                        connectionAsyncTask = getAsyncTask();
                        connectionAsyncTask.execute(new URL[]{new URL("http://192.168.1.1/graph.json?start=" + currentJsonGraphIndex)});
                    }
                } catch (MalformedURLException e) {
                    Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_SHORT).show();
                }
            } else {
                saveFlightData.hide();
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
            int startIndex = (int) jsonObject.get("startIndex");
            int totalLength = (int) jsonObject.get("historyIndex");
            if (totalLength < currentJsonGraphIndex) {
                // if the length is wrong then we have stale data and should reset the arrays
                currentJsonGraphIndex = 0;
                altitudeHistoryRms = new JSONArray();
                altitudeHistorySmoothed = new JSONArray();
                escHistoryFull = new JSONArray();
                dtHistoryFull = new JSONArray();
                altitudeHistoryFull = new JSONArray();
                temperatureHistoryFull = new JSONArray();
                return;
            }
            try {
                for (int index = 0; index < altitudeHistory.length() && startIndex + index < totalLength; index++) {
                    final Double value = altitudeHistory.getDouble(index);
                    //currentJsonGraphIndex++;
                    //dataLength++;
                    value5 = value4;
                    value4 = value3;
                    value3 = value2;
                    value2 = value1;
                    value1 = value0;
                    value0 = value;
                    altitudeHistoryRms.put(startIndex + index, Math.sqrt((value5 * value5 + value4 * value4 + value3 * value3 + value2 * value2 + value1 * value1 + value0 * value0) / 6));
                    altitudeHistorySmoothed.put(startIndex + index, (value5 * 0.1 + value4 * 0.1 + value3 * 0.2 + value2 * 0.3 + value1 * 0.2 + value0 * 0.1));
                    escHistoryFull.put(startIndex + index, escHistory.getInt(index));
                    dtHistoryFull.put(startIndex + index, dtHistory.getInt(index));
                    altitudeHistoryFull.put(startIndex + index, altitudeHistory.getDouble(index));
                    temperatureHistoryFull.put(startIndex + index, temperatureHistory.getDouble(index));
                }
            } catch (JSONException e) {
//                Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_SHORT).show();
            }
            //.splice(" + startIndex + "," + dataLength + "," + 
            currentJsonGraphIndex = altitudeHistoryFull.length();
            //currentJsonGraphIndex = (currentJsonGraphIndex > totalLength) ? totalLength : currentJsonGraphIndex;
            myWebView.loadUrl("javascript:flightChart.data.labels = new Array(" + totalLength + ");");
            myWebView.loadUrl("javascript:flightChart.data.datasets[0].data = " + altitudeHistoryFull.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[1].data = " + altitudeHistoryRms.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[2].data = " + altitudeHistorySmoothed.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[3].data = " + temperatureHistoryFull.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[4].data = " + escHistoryFull.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.data.datasets[5].data = " + dtHistoryFull.toString() + ";");
            myWebView.loadUrl("javascript:flightChart.update();");
            synchronized (jsonDataLock) {
                // todo: add the remaining entries and test the results of saving and loading saved data
                jsonObject.put("altitudeHistory", altitudeHistoryFull);
                jsonObject.put("temperatureHistory", temperatureHistoryFull);
                jsonObject.put("escHistory", escHistoryFull);
                jsonObject.put("dtHistory", dtHistoryFull);
                jsonObject.put("startIndex", 0);
                sharedJsonData = jsonObject.toString();
            }
        } catch (JSONException e) {
            Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_SHORT).show();
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
        myWebView.getSettings().setBuiltInZoomControls(true);
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
//                        inputStream = openFileInput(appDrawListAdapter.getItem(position));
                        final File sdCard = Environment.getExternalStorageDirectory();
                        final File flightLogsDirectory = new File(sdCard.getAbsolutePath() + "/FlightLogs");
                        final File file = new File(flightLogsDirectory, appDrawListAdapter.getItem(position));
                        inputStream = new FileInputStream(file);
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
                    Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_SHORT).show();
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
                appDrawListAdapter.clear();
                appDrawListAdapter.add("demo data 1");
                appDrawListAdapter.add("demo data 2");
                appDrawListAdapter.add("demo data 3");
                if (PackageManager.PERMISSION_GRANTED != ContextCompat.checkSelfPermission(getApplicationContext(), Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
                    ActivityCompat.requestPermissions(MainActivity.this, new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, 123);
                } else {
                    final File sdCard = Environment.getExternalStorageDirectory();
                    final File flightLogsDirectory = new File(sdCard.getAbsolutePath() + "/FlightLogs");
//                final String[] fileNameArray = fileList();
                    final String[] fileNameArray = flightLogsDirectory.list();
                    if (fileNameArray != null) {
                        Arrays.sort(fileNameArray);
                        appDrawListAdapter.addAll(Arrays.asList(fileNameArray));
                    }
                }
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
                    final File sdCard = Environment.getExternalStorageDirectory();
                    final File flightLogsDirectory = new File(sdCard.getAbsolutePath() + "/FlightLogs");
                    flightLogsDirectory.mkdirs();
                    final SimpleDateFormat simpleDateFormat = new SimpleDateFormat("dd-MMM-yyyy hh:mm");
                    final File file = new File(flightLogsDirectory, simpleDateFormat.format(new Date()) + ".json");
                    final FileOutputStream fileOutputStream = new FileOutputStream(file);
//                    openFileOutput(simpleDateFormat.format(new Date()), Context.MODE_PRIVATE);
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
                Snackbar.make(view, "Flight Graphs", Snackbar.LENGTH_SHORT).setAction("Action", null).show();
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
                //saveFlightData.hide();
                //myWebView.loadUrl("http://192.168.1.1/telemetry");
                Snackbar.make(view, wifiMessage, Snackbar.LENGTH_SHORT).setAction("Action", null).show();
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
