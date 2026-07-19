/**
 * @file Chat.ino
 * @brief Hauptdatei für den CardijnChat - Lokaler Echtzeit-Chat mit Captive Portal.
 *
 * Verwaltet Netzwerk-Boot, Captive Portal Weiterleitungen und delegiert alle Updates
 * an die spezialisierten Sub-Manager.
 */

#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "WebAssets.h"
#include "DNSServer.h"
#include "ChatManager.h"

// ---------- Globale Instanzen ----------
AsyncWebServer server(Config::HTTP_PORT);
CustomDNSServer dnsServer;
ChatManager chatManager;

IPAddress apIP(10, 10, 10, 1);
IPAddress subnet(255, 255, 255, 0);

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
    // Dies sichert die fehlerfreie Verbindung des WebSockets!
    String hostHeader = request->host();
    if (hostHeader != "10.10.10.1" && hostHeader != "10.10.10.1:80") {
        handleRedirect(request);
        return;
    }

    // Senden des vor-komprimierten SPA-Frontends aus dem PROGMEM
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
    Serial.begin(115200);
    Serial.println("\n====================================");
    Serial.println("CardijnChat wird gestartet...");
    Serial.println("====================================");

    // Initialisierung des Zufallszahlengenerators
    randomSeed(analogRead(0));

    // WLAN-Access Point (AP) konfigurieren auf festem Kanal
    Serial.print("Konfiguriere Access Point: ");
    Serial.println(Config::CHATNAME);

    // Setze maximale Sendeleistung und deaktiviere Stromsparmodi für maximale Leistung
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setOutputPower(Config::MAX_WIFI_POWER);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, subnet);
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

    // Captive Portal Endpunkte für automatische OS-Erkennung
    server.on("/generate_204", HTTP_GET, handleRedirect);            // Android
    server.on("/hotspot-detect.html", HTTP_GET, handleRedirect);     // Apple iOS/macOS
    server.on("/library/test/success.html", HTTP_GET, handleRedirect); // Apple iOS/macOS
    server.on("/success.txt", HTTP_GET, handleRedirect);             // Apple iOS/macOS
    server.on("/connecttest.txt", HTTP_GET, handleRedirect);         // Windows
    server.on("/ncsi.txt", HTTP_GET, handleRedirect);                // Windows

    // Fallback/Captive-Portal Handler für unbekannte Routen
    server.onNotFound([](AsyncWebServerRequest *request) {
        String hostHeader = request->host();
        if (hostHeader != "10.10.10.1" && hostHeader != "10.10.10.1:80") {
            handleRedirect(request);
        } else {
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
    // DNS-Pakete verarbeiten
    dnsServer.process();

    // Zentrales Update für alle Sub-Manager (WebSocket, Mesh, OLED, LED)
    chatManager.update();
}
