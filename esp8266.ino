#include <ArduinoJson.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <SPI.h>
#include <SD.h>

#define _ERR 0x00
#define _NO_WIFI 0x01
#define _OK 0x02
#define startByte '<'
#define stopByte '>'
#define maxNumbers 20
#define BAUD 74880
#define SS 15
#define RST 0

SoftwareSerial stm32(4, 5); //Rx, Tx

String _KEY = "01123581321345589144233";

String ssidArray[maxNumbers];
String passArray[maxNumbers];

struct Status {
    bool time;
    bool device;
    bool network;
    bool location;
    bool setLocation;
    bool session;
};

struct Config {
    String ssid;
    String pass;
    int numsWIFI;
    unsigned int newTimeMeas;
    unsigned int newNumbMeas;
    unsigned int newTimeBreak;
    unsigned int currentTimeMeas;
    unsigned int currentNumbMeas;
    unsigned int currentTimeBreak;
    unsigned int timeUNIX;
};

struct Location {
    String latitude;
    String longitude;
    String accuracy;
};

String ssid = "";
String pass = "";

struct Data {
    int pm2_5;
    int pm10;
    int pm2_5C;
    int pm10C;
    String temperature;
    String humidity;
    String pressure;
};

Status status = {
    .time = false,
    .device = false,
    .network = false,
    .location = false,
    .setLocation = false,
    .session = false
};

Config config = {
    .ssid = "",
    .pass = "",
    .numsWIFI = 0,
    .newTimeMeas = 0,
    .newNumbMeas = 0,
    .newTimeBreak = 0,
    .currentTimeMeas = 30,
    .currentNumbMeas = 1,
    .currentTimeBreak = 30,
    .timeUNIX = 0
};

Location location = {
    .latitude = "",
    .longitude = "",
    .accuracy = ""
};

Data data = {
    .pm2_5 = 0,
    .pm10 = 0,
    .pm2_5C = 0,
    .pm10C = 0,
    .temperature = "0.0",
    .humidity = "0.0",
    .pressure = "0.0"
};

/*------------------------*/
byte dataArray[] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
}; // = {startByte, pm2.5_1, pm2.5_2, pm10_1, pm10_2, _t1, _t2, _t3, _t4, stopByte};
byte dataSTM[32];
bool startChar = false, stopChar = false;
int inc = 0;
unsigned long autoRequest = 30000, saveTime = 0, currentTime = 0;

void setup() {

    Serial.begin(BAUD);
    stm32.begin(BAUD);
    while (!Serial) {}
    while (!stm32) {}

    pinMode(RST, OUTPUT);
    digitalWrite(RST, LOW);
    delay(1000);
    digitalWrite(RST, HIGH);
    delay(3000);

    Serial.println("--------------START--------------");

    if (1 == loadDataConfig()) {
        status.device = true;
        if (scannerWIFI()) {
            if (connectWIFI()) {
                status.network = true;
                Serial.println("Połączono z " + ssid);
                Serial.println("Określanie lokalizacji...");
                if (getLocation()) {
                    status.location = true;
                    Serial.println("Określono lokalizacje:");
                    Serial.print("Latitude = ");
                    Serial.println(location.latitude);
                    Serial.print("Longitude = ");
                    Serial.println(location.longitude);
                    Serial.print("Accuracy = ");
                    Serial.println(location.accuracy);
                }

                Serial.println("Pobieranie danych konfiguracyjnych");
                if (getConfigMeas(true)) {
                    Serial.print("Czas pomiaru:          ");
                    Serial.print(config.currentTimeMeas);
                    Serial.print("s\nLiczba pomiarow:       ");
                    Serial.println(config.currentNumbMeas);
                    Serial.print("Czas miedzy pomiarami: ");
                    Serial.print(config.currentTimeBreak);
                    Serial.print("s\nCzas UNIX:             ");
                    Serial.println(config.timeUNIX);
                    status.time = true;
                    sendDataToSTM32(_OK, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                }
            } else {
                Serial.println("Nie udało się połączyć z siecią " + ssid);
                sendDataToSTM32(_NO_WIFI, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
            }
        } else {
            Serial.println("Nie udało się połączyć z siecią " + ssid);
            sendDataToSTM32(_NO_WIFI, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
        }
    } else {
        Serial.println("Błąd krytyczny");
        sendDataToSTM32(_ERR, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
    }

}

void loop() {

    if (status.device) {
        currentTime = millis();
        if (currentTime - saveTime >= autoRequest) {
            Serial.println("---------------------------------------------------------------");
            if (isConnection()) {
                Serial.println("System połączony z siecią");
                if (!status.location) {
                    Serial.println("Określanie lokalizacji...");
                    if (getLocation()) {
                        status.location = true;
                        Serial.println("Określono lokalizacje:");
                        Serial.print("Latitude = ");
                        Serial.println(location.latitude);
                        Serial.print("Longitude = ");
                        Serial.println(location.longitude);
                        Serial.print("Accuracy = ");
                        Serial.println(location.accuracy);
                    }

                    if (getConfigMeas(true)) {
                        if (isChange()) {
                            Serial.println("Zmiana danych konfiguracyjnych");
                            Serial.print("Czas pomiaru:          ");
                            Serial.print(config.currentTimeMeas);
                            Serial.print("s\nLiczba pomiarow:       ");
                            Serial.println(config.currentNumbMeas);
                            Serial.print("Czas miedzy pomiarami: ");
                            Serial.print(config.currentTimeBreak);
                            Serial.println("s");
                            sendDataToSTM32(_OK, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                        } else {
                            Serial.println("Brak zmiany danych konfiguracyjnych");
                        }
                    }

                } else {
                    Serial.println("Lokalizacja określona");
                    if (getConfigMeas(false)) {
                        if (isChange()) {
                            Serial.println("Zmiana danych konfiguracyjnych");
                            Serial.print("Czas pomiaru:          ");
                            Serial.print(config.currentTimeMeas);
                            Serial.print("s\nLiczba pomiarow:       ");
                            Serial.println(config.currentNumbMeas);
                            Serial.print("Czas miedzy pomiarami: ");
                            Serial.print(config.currentTimeBreak);
                            Serial.println("s");
                            sendDataToSTM32(_OK, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                        } else {
                            Serial.println("Brak zmiany danych konfiguracyjnych");
                        }
                    }
                }
            } else {
                Serial.println("System nie połączony z siecią");
                if (scannerWIFI()) {
                    if (connectWIFI()) {
                        Serial.println("System połączony z siecią");
                        if (!status.location) {
                            Serial.println("Określanie lokalizacji...");
                            if (getLocation()) {
                                status.location = true;
                                Serial.println("Określono lokalizacje:");
                                Serial.print("Latitude = ");
                                Serial.println(location.latitude);
                                Serial.print("Longitude = ");
                                Serial.println(location.longitude);
                                Serial.print("Accuracy = ");
                                Serial.println(location.accuracy);
                            }

                            if (getConfigMeas(true)) {
                                if (isChange()) {
                                    Serial.println("Zmiana danych konfiguracyjnych");
                                    Serial.print("Czas pomiaru:          ");
                                    Serial.print(config.currentTimeMeas);
                                    Serial.print("s\nLiczba pomiarow:       ");
                                    Serial.println(config.currentNumbMeas);
                                    Serial.print("Czas miedzy pomiarami: ");
                                    Serial.print(config.currentTimeBreak);
                                    Serial.println("s");
                                    sendDataToSTM32(_OK, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                                } else {
                                    sendDataToSTM32(_OK, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                                    Serial.println("Brak zmiany danych konfiguracyjnych");
                                }
                            }

                        } else {
                            Serial.println("Lokalizacja określona");
                            if (getConfigMeas(false)) {
                                if (isChange()) {
                                    Serial.println("Zmiana danych konfiguracyjnych");
                                    Serial.print("Czas pomiaru:          ");
                                    Serial.print(config.currentTimeMeas);
                                    Serial.print("s\nLiczba pomiarow:       ");
                                    Serial.println(config.currentNumbMeas);
                                    Serial.print("Czas miedzy pomiarami: ");
                                    Serial.print(config.currentTimeBreak);
                                    Serial.println("s");
                                    sendDataToSTM32(_OK, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                                } else {
                                    sendDataToSTM32(_OK, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                                    Serial.println("Brak zmiany danych konfiguracyjnych");
                                }
                            }
                        }
                    } else {
                        Serial.println("Brak połączenia z internetem");
                        sendDataToSTM32(_NO_WIFI, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                    }
                } else {
                    Serial.println("Brak połączenia z internetem");
                    sendDataToSTM32(_NO_WIFI, config.currentTimeMeas, config.currentNumbMeas, config.currentTimeBreak, config.timeUNIX);
                }

            }

            Serial.println("---------------------------------------------------------------");
            Serial.println("");
            saveTime = currentTime;
        }
    }

    if (stm32.available() > 0) {
        char c;
        c = stm32.read();
        if (c == stopByte && inc > 25) {

            int i = 0;
            startChar = false;
            int code = int(dataSTM[1]);
            data.pm2_5 = int(dataSTM[2] << 8 | dataSTM[3]);
            data.pm10 = int(dataSTM[4] << 8 | dataSTM[5]);
            data.pm2_5C = int(dataSTM[6] << 8 | dataSTM[7]);
            data.pm10C = int(dataSTM[8] << 8 | dataSTM[9]);
            char dateSave[] = "     ";
            int counter = 0;

            if (code == 1) {
                dateSave[0] = dataSTM[17];
                dateSave[1] = dataSTM[18];
                dateSave[2] = dataSTM[19];
                dateSave[3] = dataSTM[20];
                dateSave[4] = dataSTM[21];
                dateSave[5] = dataSTM[22];
                dateSave[6] = dataSTM[23];
                dateSave[7] = dataSTM[24];
                dateSave[8] = dataSTM[25];
                dateSave[9] = dataSTM[26];
                dateSave[10] = dataSTM[27];
                dateSave[11] = dataSTM[28];
                dateSave[12] = dataSTM[29];
                dateSave[13] = dataSTM[30];
            } else {
                counter = int(dataSTM[26] << 32 | dataSTM[27] << 24 | dataSTM[28] << 16 | dataSTM[29] << 8 | dataSTM[30]);
            }

            Serial.println(dateSave);
            int temp1 = int(dataSTM[10]);
            int temp2 = int(dataSTM[11]);
            String stemp1 = String(temp1);
            String stemp2 = String(temp2);
            if (temp2 < 10) {
                data.temperature = stemp1 + ".0" + stemp2;
            } else {
                data.temperature = stemp1 + "." + stemp2;
            }

            int pres1 = int(dataSTM[12] << 8 | dataSTM[13]);
            int pres2 = int(dataSTM[14]);
            String spres1 = String(pres1);
            String spres2 = String(pres2);
            if (pres2 < 10) {
                data.pressure = spres1 + ".0" + spres2;
            } else {
                data.pressure = spres1 + "." + spres2;
            }

            int humi1 = int(dataSTM[15]);
            int humi2 = int(dataSTM[16]);
            String shumi1 = String(humi1);
            String shumi2 = String(humi2);
            if (humi2 < 10) {
                data.humidity = shumi1 + ".0" + shumi2;
            } else {
                data.humidity = shumi1 + "." + shumi2;
            }

            Serial.println("=-------------------------------=");
            Serial.print("PM2.5: ");
            Serial.println(data.pm2_5);
            Serial.print("PM10: ");
            Serial.println(data.pm10);
            Serial.print("PM2.5C: ");
            Serial.println(data.pm2_5C);
            Serial.print("PM10C: ");
            Serial.println(data.pm10C);
            Serial.print("Temperature: ");
            Serial.println(data.temperature);
            Serial.print("Pressure: ");
            Serial.println(data.pressure);
            Serial.print("Humidity: ");
            Serial.println(data.humidity);
            Serial.print("Latitude: ");
            Serial.println(location.latitude);
            Serial.print("Longitude: ");
            Serial.println(location.longitude);
            Serial.println("=-------------------------------=");

            if (!isConnection()) {
                if (scannerWIFI()) {
                    if (connectWIFI()) {
                        if (!status.session) {
                            status.session = true;
                            saveDataToDB(false);
                        } else {
                            saveDataToDB(true);
                        }
                    }
                }
            } else {
                if (!status.session) {
                    status.session = true;
                    saveDataToDB(false);
                } else {
                    saveDataToDB(true);
                }
            }

            saveDataToSD(code, dateSave, counter);

        }

        if (startChar) {
            dataSTM[inc] = c;
        }

        if (c == startByte && !startChar) {
            startChar = true;
            inc = 0;
            dataSTM[inc] = c;
        }
        inc++;
    }
}

int loadDataConfig() {
    Serial.print("Inicjalizacja karty SD ... ");
    if (!SD.begin(SS)) {
        Serial.println("nie powiodla sie!");
        return -1;
    } else {
        Serial.println(" powiodla sie!");
        if (SD.exists("config.txt")) {
            Serial.println("Plik config.txt istnieje.\nZawiera:");
            File configFile = SD.open("config.txt");
            int j = 0;
            while (configFile.available()) {
                String data = configFile.readStringUntil(';');
                ssidArray[j] = data.substring(0, data.indexOf(' '));
                passArray[j] = data.substring(data.indexOf(' ') + 1);
                Serial.println(ssidArray[j] + " -> " + passArray[j]);
                j++;
                config.numsWIFI++;
            }
            Serial.print("Liczba zapisanych sieci: ");
            Serial.println(config.numsWIFI);
            configFile.close();
            return 1;
        } else {
            return -2;
        }
    }
}

bool scannerWIFI() {
    int maxRSSI = -99999;
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    if (n == 0) {
        return false;
    } else {
        for (int i = 0; i < n; ++i) {
            Serial.print(WiFi.SSID(i));
            Serial.print(" ");
            Serial.println(WiFi.RSSI(i));
            for (int j = 0; j < config.numsWIFI; ++j) {
                if (WiFi.SSID(i).equals(ssidArray[j])) {
                    if (maxRSSI < WiFi.RSSI(i)) {
                        maxRSSI = WiFi.RSSI(i);
                        ssid = ssidArray[j];
                        pass = passArray[j];
                    }
                }
            }
        }

        if (maxRSSI != -99999) {
            Serial.println("Wykryto siec WIFI: " + ssid + "(" + pass + ")");
            return true;
        } else {
            Serial.println("Nie wykryto żadnych dostępnych sieci WIFI");
            return false;
        }
    }
}

bool connectWIFI() {
    char ssidS[ssid.length() + 1];
    char passS[pass.length() + 1];
    ssid.toCharArray(ssidS, ssid.length() + 1);
    pass.toCharArray(passS, pass.length() + 1);

    WiFi.begin(ssidS, passS);
    Serial.print("Connecting");
    int i = 0;
    while (WiFi.status() != WL_CONNECTED) {
        i++;
        delay(500);
        Serial.print(".");
        if (i == 50) {
            return false;
        }
    }
    Serial.println();
    Serial.println();
    return true;
}

bool getConfigMeas(bool p) {

    HTTPClient http;
    String postData = "KEY=" + _KEY + "&IS_SET_LOCATION=" + status.setLocation + "&L_LAT=" + location.latitude + "&L_LONG=" + location.longitude + "&ACC=" + location.accuracy;
    String link = "http://dustsensor.h2g.pl/configuration.php";
    http.begin(link);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(postData);
    if (httpCode > 0) {
        Serial.println();
        String r = http.getString();
        Serial.println("Pobrano dane konfiguracyjne: '" + r + "'");
        const size_t bufferSize = JSON_OBJECT_SIZE(4) + 70;
        DynamicJsonBuffer jsonBuffer(bufferSize);
        JsonObject & root = jsonBuffer.parseObject(r);
        const char * timeMeasure = root["timeMeasure"];
        const char * nMeasures = root["nMeasures"];
        const char * breakTime = root["breakTime"];
        const char * timeR = root["time"];
        const char * statusL = root["status"];

        String sTimeMeas(timeMeasure);
        String sNumbMeas(nMeasures);
        String sTimeBreak(breakTime);
        String sTimeUNIX(timeR);
        String sStatusL(statusL);

        if (sStatusL == "true") {
            status.setLocation = true;
            Serial.println("Aktualizacja lokalizacji powiodła się");
        }

        config.timeUNIX = sTimeUNIX.toInt();

        config.newTimeMeas = sTimeMeas.toInt();
        config.newNumbMeas = sNumbMeas.toInt();
        config.newTimeBreak = sTimeBreak.toInt();

        return true;
    }
    http.end();
}

bool getLocation() {

    DynamicJsonBuffer jsonBuffer;
    int n = WiFi.scanNetworks();
    String jsonString = "{\n";
    jsonString = "{\n";
    jsonString += "\"homeMobileCountryCode\": 234,\n";
    jsonString += "\"homeMobileNetworkCode\": 27,\n";
    jsonString += "\"radioType\": \"gsm\",\n";
    jsonString += "\"carrier\": \"Vodafone\",\n";
    jsonString += "\"wifiAccessPoints\": [\n";
    for (int j = 0; j < n; ++j) {
        jsonString += "{\n";
        jsonString += "\"macAddress\" : \"";
        jsonString += (WiFi.BSSIDstr(j));
        jsonString += "\",\n";
        jsonString += "\"signalStrength\": ";
        jsonString += WiFi.RSSI(j);
        jsonString += "\n";
        if (j < n - 1) {
            jsonString += "},\n";
        } else {
            jsonString += "}\n";
        }
    }
    jsonString += ("]\n");
    jsonString += ("}\n");

    WiFiClientSecure client;

    if (client.connect("www.googleapis.com", 443)) {
        Serial.println("Połączono z serwerem.");
        client.println("POST /geolocation/v1/geolocate?key=YOUR_KEY HTTP/1.1");
        client.println("Host: www.googleapis.com");
        client.println("Connection: close");
        client.println("Content-Type: application/json");
        client.println("User-Agent: Arduino/1.0");
        client.print("Content-Length: ");
        client.println(jsonString.length());
        client.println();
        client.print(jsonString);
        delay(500);
    }

    while (client.available()) {
        String line = client.readStringUntil('\r');
        JsonObject & root = jsonBuffer.parseObject(line);
        if (root.success()) {
            float f_latitude = root["location"]["lat"];
            float f_longitude = root["location"]["lng"];
            float f_accuracy = root["accuracy"];

            location.latitude = String(f_latitude, 6);
            location.longitude = String(f_longitude, 6);
            location.accuracy = String(f_accuracy);

        }
    }
    client.stop();

    return true;
}

void sendDataToSTM32(byte code, int timeM, int nTime, int breakT, int UNIX) {

    byte _byteTimeM = timeM;
    byte _byteNTime = nTime;
    byte _byteBreakT = breakT;

    byte _t4 = UNIX & 0x000000ff;
    byte _t3 = (UNIX & 0x0000ff00) >> 8;
    byte _t2 = (UNIX & 0x00ff0000) >> 16;
    byte _t1 = (UNIX & 0xff000000) >> 24;

    byte data[] = {
        startByte,
        code,
        _byteTimeM,
        _byteNTime,
        _byteBreakT,
        _t1,
        _t2,
        _t3,
        _t4,
        stopByte
    };

    stm32.write(data, sizeof(data));
    Serial.write(data, sizeof(data));
    Serial.println("");
}

bool isChange() {
    if (config.currentTimeMeas == config.newTimeMeas && config.currentNumbMeas == config.newNumbMeas && config.currentTimeBreak == config.newTimeBreak) {
        return false;
    } else {
        config.currentTimeMeas = config.newTimeMeas;
        config.currentNumbMeas = config.newNumbMeas;
        config.currentTimeBreak = config.newTimeBreak;
        return true;
    }
}

bool isConnection() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    } else {
        return false;
    }
}

bool saveDataToDB(bool session) {
    Serial.println("Zapisywanie..");
    HTTPClient http;
    String key = _KEY;
    String postData = "KEY=" + key + "&PM2_5=" + data.pm2_5 + "&PM10=" + data.pm10 + "&PM2_5_CORR=" + data.pm2_5C + "&PM10_CORR=" + data.pm10C + "&TEMP=" + data.temperature + "&PRES=" + data.pressure + "&HUMI=" + data.humidity + "&LATI=" + location.latitude + "&LONGI=" + location.longitude + "&SESSION=" + session;
    String link = "http://dustsensor.h2g.pl/save.php";
    http.begin(link);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(postData);
    if (httpCode > 0) {
        Serial.println();
        String r = http.getString();
        Serial.println("Pobrano dane: " + r);
        Serial.println("Zapisywano!");
        return true;
    }
    http.end();
}

bool saveDataToSD(int code, String dateSave, int count) {

    Serial.print("Inicjalizacja karty SD ... ");
    if (!SD.begin(SS)) {
        Serial.println("nie powiodła się!");
    } else {
        if (code) {
            String timeS = dateSave.substring(0, 5);
            String nameFile = dateSave.substring(6, 12) + dateSave.substring(12, dateSave.length());
            nameFile.replace("/", "-");
            File file = SD.open(nameFile + ".txt", FILE_WRITE);
            if (file) {
                Serial.println("\nZapis do pliku...:");
                String s = timeS + "\t\t" + String(data.pm2_5C) + "ug/m3\t\t" + String(data.pm10C) + "ug/m3\t\t" + data.temperature + "'C\t\t" + data.pressure + "hPa\t\t" + data.humidity + "%";
                Serial.println(s);
                file.println(s);
                Serial.println("Zapisano!\n");
            } else {
                Serial.println("Bład otwarcia pliku: " + nameFile + ".txt");
            }

            file.close();
        } else {
            File file = SD.open("NOTIME.txt", FILE_WRITE);
            if (file) {
                Serial.println("\nZapis do pliku...:");
                String s = String(count) + "\t\t" + String(data.pm2_5) + "ug/m3\t\t" + String(data.pm10) + "ug/m3\t\t" + data.temperature + "'C\t\t" + data.pressure + "hPa\t\t" + data.humidity + "%";
                Serial.println(s);
                file.println(s);
                Serial.println("Zapisano!\n");
            }

            file.close();
        }

    }

}