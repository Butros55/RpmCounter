#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

void setup()
{
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("=== BLE Scanner – bitte OBD-Dongle einschalten ===");

    BLEDevice::init("ESP32-Scanner");
}

void loop()
{
    Serial.println("Starte Scan (5s)...");

    BLEScan *pScan = BLEDevice::getScan();
    pScan->setActiveScan(true); // aktiver Scan: mehr Infos
    pScan->setInterval(100);
    pScan->setWindow(80);

    // 5 Sekunden scannen
    BLEScanResults results = pScan->start(5, false);

    int count = results.getCount();
    Serial.print("Gefundene Geräte: ");
    Serial.println(count);

    for (int i = 0; i < count; i++)
    {
        BLEAdvertisedDevice dev = results.getDevice(i);

        Serial.println("---- Gerät ----");

        // MAC-Adresse
        std::string addr = dev.getAddress().toString();
        Serial.print("Adresse: ");
        Serial.println(addr.c_str());

        // Name (kann auch leer sein)
        std::string name = dev.getName();
        Serial.print("Name   : ");
        if (!name.empty())
        {
            Serial.println(name.c_str());
        }
        else
        {
            Serial.println("(kein Name)");
        }

        // Service-UUID, falls in den Advertising-Daten
        if (dev.haveServiceUUID())
        {
            Serial.print("Service UUID: ");
            Serial.println(dev.getServiceUUID().toString().c_str());
        }
        else
        {
            Serial.println("Service UUID: (keine im Advertising)");
        }

        Serial.println("------------------------");
    }

    Serial.println("======== Scan fertig ========\n");

    // kurze Pause, dann nächster Scan
    delay(5000);
}
