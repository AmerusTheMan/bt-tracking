#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>

#include <arduino_secrets.h>

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
    httpUpdate.setLedPin(LED_BUILTIN, LOW);
    httpUpdate.onStart(event_ota_start);
    httpUpdate.onEnd(event_ota_end);
    httpUpdate.onProgress(event_ota_progress);
    httpUpdate.onError(event_ota_error);
    t_httpUpdate_return ret = httpUpdate.update(client, SERVER_HOST, SERVER_PORT, "/file.bin");
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            logger.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;

        case HTTP_UPDATE_NO_UPDATES:
            logger.println("HTTP_UPDATE_NO_UPDATES");
            break;

        case HTTP_UPDATE_OK:
            logger.println("HTTP_UPDATE_OK");
            break;
    }
}

//
// timekeeping
//
int64_t time_offset = 0;

void fetch_time_offset() {
    HTTPClient http;
    http.begin(client, SERVER_HOST, SERVER_PORT, "/time");

    int resp = http.GET();
    if (resp == 200) {
        String data = http.getString();
        sscanf(data.c_str(), "%lld", &time_offset);
        time_offset -= esp_timer_get_time() / 1000;
        logger.println("got time offset");
    } else {
        logger.printf("error code: %d\n", resp);
    }
}

int64_t get_time() {
    return time_offset + esp_timer_get_time() / 1000;
}

//
// ze bluetooh device
//
BLEScan *pBLEScan;

/*class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        logger.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
    }
};*/

void ble_setup() {
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();  //create new scan
    //pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);  //active scan uses more power, but get results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);  // less or equal setInterval value
}

void ble_scan() {
    BLEScanResults *foundDevices = pBLEScan->start(5, false); // seconds
    logger.printf("Devices found: %d\n", foundDevices->getCount());
    logger.println("Scan done!");
    pBLEScan->clearResults();  // delete results fromBLEScan buffer to release memory
}

//
// startup task and events
//

void event_got_ip(WiFiEvent_t event, WiFiEventInfo_t info) {
    logger.println("WiFi connected");
    logger.println(IPAddress(info.got_ip.ip_info.ip.addr));

    logger.setup();
    fetch_time_offset();
    fetch_update();
    ble_setup();
}

void setup() {
    Serial.begin(115200);

    for (uint8_t t = 4; t > 0; t--) {
        logger.printf("[SETUP] WAIT %u...\n", t);
        Serial.flush();
        delay(1000);
    }

    // reset config
    WiFi.disconnect(true);
    // listen for ip event
    WiFi.onEvent(event_got_ip, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    // connect
    WiFi.begin(SECRET_SSID, SECRET_PASS);

    logger.println("connecting...");
}

//
// main loop
//

void loop() {
    // process incoming tcp requests to logger
    logger.accept();
    // DEBUG log time and wait
    logger.printf("%lld\n", get_time());
    delay(1000);
}
