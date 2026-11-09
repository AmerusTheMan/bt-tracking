#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <RTClib.h>

#include <arduino_secrets.h>

// REMEMBER TO SET TO "1" IN RELEASE!!!!!!!!!!!
#define RELEASE_BUILD 1

//
// logging
//
class Logger: public Print {
    NetworkServer server;
    NetworkClient client;

    public:
        // server stuff
        void setup() {
            server = NetworkServer(6969);
            server.begin();
        }
        void accept() {
            // accept new logging client
            NetworkClient new_client = server.accept();
            if (new_client) {
                client = new_client;
            }
        }

        // logging overrides
        size_t write(const uint8_t data) override {
            size_t ret = Serial.write(data);
            if (client) {
                client.write(data);
            }
            return ret;
        }
        size_t write(const uint8_t *buffer, size_t size) override {
            size_t ret = Serial.write(buffer, size);
            if (client) {
                client.write(buffer, size);
            }
            return ret;
        }
};
Logger logger;

NetworkClient client;

//
// http ota
//
void event_ota_start() {
    logger.println("[OTA] started");
}
void event_ota_end() {
    logger.println("[OTA] finished");
}
void event_ota_progress(int cur, int total) {
    logger.printf("[OTA] %d of %d bytes...\n", cur, total);
}
void event_ota_error(int err) {
    logger.printf("[OTA] fatal error code %d\n", err);
}

void fetch_update() {
    if (RELEASE_BUILD == 1) {
        //httpUpdate.setLedPin(LED_BUILTIN, LOW);
        httpUpdate.onStart(event_ota_start);
        httpUpdate.onEnd(event_ota_end);
        httpUpdate.onProgress(event_ota_progress);
        httpUpdate.onError(event_ota_error);
        t_httpUpdate_return ret = httpUpdate.update(client, SERVER_HOST, SERVER_PORT, "/file.bin");
        switch (ret) {
            case HTTP_UPDATE_FAILED:
                logger.printf("[OTA] HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                break;
            case HTTP_UPDATE_NO_UPDATES:
                logger.println("[OTA] HTTP_UPDATE_NO_UPDATES");
                break;
            case HTTP_UPDATE_OK:
                logger.println("[OTA] HTTP_UPDATE_OK");
                break;
        }
    } else {
        logger.println("[OTA] dev build, not updating");
    }
}

//
// timekeeping
//
bool time_synced = false;
int64_t time_offset = 0;

void fetch_time_offset() {
    HTTPClient http;
    http.begin(client, SERVER_HOST, SERVER_PORT, "/time");

    int resp = http.GET();
    if (resp == 200) {
        String data = http.getString();
        sscanf(data.c_str(), "%lld", &time_offset);
        time_offset -= esp_timer_get_time() / 1000;
        time_synced = true;
        logger.println("[TIME] got time offset");
    } else {
        logger.printf("[TIME] error code: %d\n", resp);
    }
}

int64_t get_time() {
    return time_offset + esp_timer_get_time() / 1000;
}

//
// ze bluetooh device
//
BLEScan *pBLEScan;

void ble_setup() {
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();  //create new scan
    pBLEScan->setActiveScan(true);  //active scan uses more power, but get results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);  // less or equal setInterval value
}

void chunked(NetworkClient client, char *buf, int len) {
    client.printf("%x\r\n", len);
    client.write(buf, (size_t)len);
    client.print("\r\n");
}

// a very scuffed chunked http upload
void ble_upload(BLEScanResults *devices, int64_t ts) {
    int res = client.connect(SERVER_HOST, SERVER_PORT);

    if (res != 1) {
        logger.printf("[BLE] failed to upload: %d\n", res);
        return;
    }

    // HTTP header
    client.printf(
        "POST /scans/new HTTP/1.1\r\nHost: %s:%d\r\nConnection: keep-alive\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n",
        SERVER_HOST,
        SERVER_PORT
    );

    char buffer[1024];

    // JSON start and own device info
    String mac = WiFi.macAddress();
    mac.toLowerCase();
    chunked(client, buffer, snprintf(
        buffer,
        1024,
        "{\"node_mac\":\"%s\",\"secret_key\":\"%s\",\"found_devices\":[",
        mac.c_str(),
        "super_password"
    ));

    // devices
    int count = devices->getCount();
    for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice dev = devices->getDevice(i);
        chunked(client, buffer, snprintf(
            buffer,
            1024,
            "{\"device_mac\":\"%s\",\"strength\":%d,\"time\":%lld}%s",
            dev.getAddress().toString().c_str(),
            dev.getRSSI(),
            ts,
            (i == count - 1 ? "" : ",")
        ));
    }

    // JSON end + end of transfer (known length)
    client.print("2\r\n]}\r\n0\r\n\r\n");
    client.flush();
    client.stop();
    logger.println("[BLE] data sent");
}

void ble_scan(int64_t ts) {
    logger.println("[BLE] scan start");
    BLEScanResults *foundDevices = pBLEScan->start(5, false); // seconds
    logger.printf("[BLE] devices found: %d\n", foundDevices->getCount());
    ble_upload(foundDevices, ts);
    pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
}

//
// startup task and events
//

void event_got_ip(WiFiEvent_t event, WiFiEventInfo_t info) {
    logger.print("[WLAN] connected: ");
    logger.println(IPAddress(info.got_ip.ip_info.ip.addr));

    logger.setup();
    fetch_time_offset();
    fetch_update();
    ble_setup();
}

void setup() {
    // debug log over USB
    Serial.begin(115200);

    for (uint8_t t = 4; t > 0; t--) {
        logger.printf("[INIT] wait %u...\n", t);
        Serial.flush();
        delay(1000);
    }

    // seed random with noise from analog pin
    randomSeed(analogRead(0));

    // reset config
    WiFi.disconnect(true);
    // listen for ip event
    WiFi.onEvent(event_got_ip, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    // connect
    WiFi.begin(SECRET_SSID, SECRET_PASS);

    logger.print("[WLAN] mac: ");
    logger.println(WiFi.macAddress());
    logger.println("[WLAN] connecting...");
}

//
// main loop
//

void loop() {
    // process incoming tcp requests to logger
    logger.accept();
    // check if current clock seconds is zero
    int64_t ts = get_time();
    if (DateTime(ts / 1000).second() == 0) {
        // if time fetch failed, try again
        if (!time_synced) {
            fetch_time_offset();
        } else {
            ble_scan(ts);
            // prevent accidentally running multiple times
            delay(1000);
            // random chance for time and update
            // doing all at once might be bad
            if (random(40, 45) == 42) {
                fetch_update();
                fetch_time_offset();
            }
        }
    }
    // feed the watchdog
    delay(1);
}
