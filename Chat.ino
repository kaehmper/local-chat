/**
 * @file PopupChat.ino
 * @brief Hauptdatei für den PopupChat - Lokaler Echtzeit-Chat mit Captive Portal.
 *
 * Diese Implementierung nutzt modernste Web-Technologien (Single Page Application via WebSockets)
 * und hochperformante Speicherverfahren (Gzip-komprimierte Web-Assets im PROGMEM),
 * um ein erstklassiges Benutzererlebnis auf dem ESP8266 zu bieten.
 */

#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>

#include "Config.h"
#include "WebAssets.h"
#include "DNSServer.h"
#include "ChatManager.h"
#include "SSD1306.h"

// ---------- Globale Instanzen ----------
AsyncWebServer server(Config::HTTP_PORT);
CustomDNSServer dnsServer;
ChatManager chatManager;

// SSD1306 OLED Instanz
SSD1306 oled(Config::OLED_I2C_ADDR, Config::OLED_SDA, Config::OLED_SCL);

IPAddress apIP(10, 10, 10, 1);
IPAddress subnet(255, 255, 255, 0);

unsigned long lastTick = 0;

// ---------- Webserver Handler ----------
void handleRedirect(AsyncWebServerRequest *request) {
    chatManager.registerActivity();
    Serial.println("[CaptivePortal] Umleitung auf http://10.10.10.1/");

    // Sende 302-Redirect mit Location-Header und HTML-Fallback-Body inklusive Meta-Refresh
    AsyncWebServerResponse *response = request->beginResponse(302, "text/html",
        "<html><head><meta http-equiv=\"refresh\" content=\"0;url=http://10.10.10.1/\"/></head>"
        "<body><p>Weiterleitung zu <a href=\"http://10.10.10.1/\">CardijnChat</a>...</p></body></html>"
    );
    response->addHeader("Location", "http://10.10.10.1/");
    response->addHeader("Cache-Control", "no-store, must-revalidate");
    request->send(response);
}

void handleServeIndex(AsyncWebServerRequest *request) {
    chatManager.registerActivity();

    // Falls der Host nicht unsere lokale IP-Adresse ist (z.B. bei Eingabe von neverssl.com),
    // leiten wir den Client sofort per 302-Redirect auf unsere kanonische IP http://10.10.10.1/ um.
    // Dies ändert die Browser-Adresszeile und sichert die fehlerfreie Verbindung des WebSockets!
    String hostHeader = request->host();
    if (hostHeader != "10.10.10.1" && hostHeader != "10.10.10.1:80") {
        handleRedirect(request);
        return;
    }

    // Hocheffizientes Senden des vor-komprimierten SPA-Frontends aus dem PROGMEM
    AsyncWebServerResponse *response = request->beginResponse_P(
        200,
        "text/html",
        WebAssets::INDEX_GZ,
        WebAssets::INDEX_GZ_LEN
    );
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Cache-Control", "no-store, must-revalidate");
    request->send(response);
}

// ---------- OLED Ticker- & Screensaver-Steuerung ----------
unsigned long lastOledTick = 0;
size_t currentTickerIndex = 0;
bool oledScreensaverActive = false;
int ssCol = 0;
int ssPage = 0;

void drawHeader() {
    oled.setCursor(0, 0);
    oled.print("=== [CardijnChat] ===");
}

void updateOLEDDisplay(unsigned long now) {
    if (!Config::ENABLE_OLED) return;

    // Aktivitäts-Status prüfen (Inaktivität nach Config::ACTIVITY_DURATION führt zu Screensaver)
    bool active = (now - chatManager.getLastActivityTime()) < Config::ACTIVITY_DURATION;

    if (!active) {
        static unsigned long lastUptimeUpdate = 0;
        bool forceRedraw = false;

        // Triggere sofortigen Neuaufbau beim Wechsel in den Screensaver-Modus
        if (!oledScreensaverActive) {
            oledScreensaverActive = true;
            lastOledTick = now;
            lastUptimeUpdate = now;
            oled.clear();

            // Bewege die Position des Textes zufällig / versetzt
            ssCol = (ssCol + 15) % 45; // Max. Spalte, damit Text noch auf Bildschirm passt
            ssPage = (ssPage + 1) % 6;  // Max. Page, damit dreizeiliger Text passt (ssPage + 2 <= 7)
            forceRedraw = true;
        }

        // Position alle 5 Sekunden verschieben (um Einbrennen zu verhindern)
        bool positionShifted = false;
        if (now - lastOledTick >= 5000) {
            lastOledTick = now;
            oled.clear();
            ssCol = (ssCol + 15) % 45;
            ssPage = (ssPage + 1) % 6;
            positionShifted = true;
        }

        // Uptime jede Sekunde aktualisieren
        if (forceRedraw || positionShifted || (now - lastUptimeUpdate >= 1000)) {
            lastUptimeUpdate = now;

            // Berechne die Uptime des Geräts in h m s Format
            unsigned long total_secs = now / 1000;
            unsigned int hours = total_secs / 3600;
            unsigned int minutes = (total_secs % 3600) / 60;
            unsigned int seconds = total_secs % 60;
            String runtimeStr = String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";

            // Wenn neu gezeichnet wird oder die Position wechselt, zeichne alle 3 Zeilen
            if (forceRedraw || positionShifted) {
                oled.setCursor(ssCol, ssPage);
                oled.print(Config::CHATNAME);
                oled.setCursor(ssCol, ssPage + 1);
                oled.print("10.10.10.1");
            }

            // Zeichne Uptime-Zeile (mit Leerzeichen gepolstert, um Reste zu überschreiben)
            oled.setCursor(ssCol, ssPage + 2);
            oled.print((runtimeStr + "   ").c_str());
        }
        return;
    }

    static bool showUserList = false;

    // Wenn Aktivität erkannt wurde, aber der Screensaver noch aktiv war
    if (oledScreensaverActive) {
        oledScreensaverActive = false;
        oled.clear();
        lastOledTick = 0; // Sofortiges Neuzeichnen triggern
        showUserList = false; // Zurücksetzen auf die Nachrichten- bzw. AP-Info-Ansicht
    }

    // Ticker-Rotation / Bildschirm-Wechsel alle 5 Sekunden
    if (now - lastOledTick >= 5000 || lastOledTick == 0) {
        lastOledTick = now;
        oled.clear();
        drawHeader();

        if (showUserList) {
            // Online-Nutzerliste anzeigen
            String usersStr = chatManager.getOnlineUsersString();
            oled.printWrapped(usersStr, 2, 6);
        } else {
            size_t tickerMsgCount = chatManager.getTickerMessageCount();
            if (tickerMsgCount == 0) {
                // Standby/Boot-Bildschirm bei fehlenden Nachrichten
                oled.setCursor(0, 2);
                oled.print("AP:  ");
                oled.print(Config::CHATNAME);

                oled.setCursor(0, 4);
                oled.print("IP:  10.10.10.1");

                oled.setCursor(0, 6);
                oled.print("Warte auf Chat...");
            } else {
                // Die letzten 3 eindeutigen Nachrichten zusammen anzeigen (älteste oben, neueste unten)
                String msgStr = "";
                for (size_t i = 0; i < tickerMsgCount; ++i) {
                    msgStr += chatManager.getLastTickerMessage(tickerMsgCount - 1 - i);
                    if (i < tickerMsgCount - 1) {
                        msgStr += "\n";
                    }
                }
                oled.printWrapped(msgStr, 1, 7);
            }
        }

        // Zustand umschalten für den nächsten Wechsel
        showUserList = !showUserList;
    }
}

// ==================== SETUP ====================
void setup() {
    // Serieller Monitor für Debug-Ausgaben initialisieren
    Serial.begin(115200);
    Serial.println("\n====================================");
    Serial.println("PopupChat wird gestartet...");
    Serial.println("====================================");

    // Initialisierung des Hardware-Zufallszahlengenerators
    randomSeed(analogRead(0));

    // OLED-Display-Schnittstelle initialisieren
    if (Config::ENABLE_OLED) {
        Serial.println("Initialisiere SSD1306 OLED-Display...");
        if (oled.begin()) {
            Serial.println("OLED-Display erfolgreich gestartet!");
        } else {
            Serial.println("OLED-Display konnte nicht initialisiert werden!");
        }
    }

    // Aktivitäts-LED konfigurieren
    pinMode(Config::ACTIVITY_LED, OUTPUT);
    digitalWrite(Config::ACTIVITY_LED, Config::ACTIVITY_REVERSE ? HIGH : LOW);

    // WLAN-Access Point (AP) konfigurieren
    Serial.print("Konfiguriere Access Point: ");
    Serial.println(Config::CHATNAME);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, subnet);
    // Start AP on fixed channel so ESP-NOW matches the frequency
    WiFi.softAP(Config::CHATNAME, nullptr, Config::MESH_CHANNEL);

    Serial.print("AP IP-Adresse: ");
    Serial.println(WiFi.softAPIP());

    // DNS-Server starten (leitet jede Anfrage auf die AP-IP um)
    Serial.println("Starte DNS-Server...");
    dnsServer.begin(Config::DNS_PORT, apIP);

    // Chat- und WebSocket-Manager starten
    Serial.println("Initialisiere Chat-Manager...");
    chatManager.begin(&server);

    // Web-Routen registrieren
    server.on("/", HTTP_GET, handleServeIndex);

    // Explizite Captive Portal Endpoints für automatische Erkennung auf allen Betriebssystemen
    server.on("/generate_204", HTTP_GET, handleRedirect);            // Android
    server.on("/hotspot-detect.html", HTTP_GET, handleRedirect);     // Apple iOS/macOS
    server.on("/library/test/success.html", HTTP_GET, handleRedirect); // Apple iOS/macOS
    server.on("/success.txt", HTTP_GET, handleRedirect);             // Apple iOS/macOS
    server.on("/connecttest.txt", HTTP_GET, handleRedirect);         // Windows
    server.on("/ncsi.txt", HTTP_GET, handleRedirect);                // Windows

    // Fallback/Captive-Portal Handler für unbekannte Routen
    server.onNotFound([](AsyncWebServerRequest *request) {
        // Überprüfe den Host-Header. Wenn er nicht mit unserer IP übereinstimmt,
        // leiten wir den Browser per 302-Redirect direkt auf unsere IP-Adresse um.
        // Dies ändert die Adresszeile des Browsers auf 10.10.10.1, sodass der
        // WebSocket-Client im Browser direkt dorthin verbinden kann!
        String hostHeader = request->host();
        if (hostHeader != "10.10.10.1" && hostHeader != "10.10.10.1:80") {
            handleRedirect(request);
        } else {
            // Falls der Host bereits 10.10.10.1 ist, aber die Route unbekannt war,
            // liefern wir das Frontend aus, um unschöne 404-Fehler zu vermeiden.
            handleServeIndex(request);
        }
    });

    // Webserver starten
    Serial.println("Starte Webserver...");
    server.begin();

    Serial.println("System erfolgreich gestartet und betriebsbereit!");
    chatManager.registerActivity();
}

// ==================== LOOP ====================
void loop() {
    // DNS-Server-Pakete verarbeiten
    dnsServer.process();

    // WebSocket-Ressourcen regelmäßig aufräumen
    chatManager.cleanup();

    // OLED-Display und Newsticker-Rotation aktualisieren
    unsigned long currentMillis = millis();
    updateOLEDDisplay(currentMillis);

    // LED Takt- und Aktivitätsanzeige
    if (currentMillis - lastTick >= Config::TICK_INTERVAL) {
        lastTick = currentMillis;

        // Prüfen, ob innerhalb des Aktivitäts-Fensters Interaktionen stattfanden
        bool active = (currentMillis - chatManager.getLastActivityTime()) < Config::ACTIVITY_DURATION;

        // LED-Status berechnen und setzen
        bool ledState = active ^ Config::ACTIVITY_REVERSE;
        digitalWrite(Config::ACTIVITY_LED, ledState ? HIGH : LOW);
    }
}
