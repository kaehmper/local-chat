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

// ---------- Globale Instanzen ----------
AsyncWebServer server(Config::HTTP_PORT);
CustomDNSServer dnsServer;
ChatManager chatManager;

IPAddress apIP(10, 10, 10, 1);
IPAddress subnet(255, 255, 255, 0);

unsigned long lastTick = 0;

// ---------- Webserver Handler ----------
void handleServeIndex(AsyncWebServerRequest *request) {
    chatManager.registerActivity();

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

// ==================== SETUP ====================
void setup() {
    // Serieller Monitor für Debug-Ausgaben initialisieren
    Serial.begin(115200);
    Serial.println("\n====================================");
    Serial.println("PopupChat wird gestartet...");
    Serial.println("====================================");

    // Initialisierung des Hardware-Zufallszahlengenerators
    randomSeed(analogRead(0));

    // Aktivitäts-LED konfigurieren
    pinMode(Config::ACTIVITY_LED, OUTPUT);
    digitalWrite(Config::ACTIVITY_LED, Config::ACTIVITY_REVERSE ? HIGH : LOW);

    // WLAN-Access Point (AP) konfigurieren
    Serial.print("Konfiguriere Access Point: ");
    Serial.println(Config::CHATNAME);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, subnet);
    WiFi.softAP(Config::CHATNAME);

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

    // Fallback/Captive-Portal Handler für unbekannte Routen
    server.onNotFound([](AsyncWebServerRequest *request) {
        // Logge Captive-Portal Versuche
        Serial.print("Anfrage an nicht existierende URL: ");
        Serial.println(request->url());

        // Jede nicht gefundene Anfrage (insb. Captive-Portal-Prüfungen der Smartphones)
        // beantworten wir direkt mit unserem SPA-Frontend. Das spart Ladezeit und Redirects!
        handleServeIndex(request);
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

    // LED Takt- und Aktivitätsanzeige
    unsigned long currentMillis = millis();
    if (currentMillis - lastTick >= Config::TICK_INTERVAL) {
        lastTick = currentMillis;

        // Prüfen, ob innerhalb des Aktivitäts-Fensters Interaktionen stattfanden
        bool active = (currentMillis - chatManager.getLastActivityTime()) < Config::ACTIVITY_DURATION;

        // LED-Status berechnen und setzen
        bool ledState = active ^ Config::ACTIVITY_REVERSE;
        digitalWrite(Config::ACTIVITY_LED, ledState ? HIGH : LOW);
    }
}
