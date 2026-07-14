#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "Config.h"

/**
 * @class MessageRingBuffer
 * @brief Eine hocheffiziente Ringpuffer-Implementierung für Chat-Nachrichten.
 * 
 * Vermeidet teure dynamische Speicherverschiebungen durch rotierende Indizes.
 */
class MessageRingBuffer {
public:
    MessageRingBuffer();

    /**
     * @brief Fügt eine Nachricht dem Ringpuffer hinzu.
     * @param msg Die Nachricht.
     */
    void add(const String& msg);

    /**
     * @brief Gibt die Anzahl der aktuell gespeicherten Nachrichten zurück.
     */
    size_t size() const;

    /**
     * @brief Ruft eine Nachricht an einer bestimmten relativen Position ab (0 = älteste).
     */
    String get(size_t index) const;

private:
    char _buffer[Config::MAX_MESSAGES][Config::MAX_MSG_LENGTH + 1];
    size_t _start;
    size_t _count;
};

/**
 * @class ChatManager
 * @brief Verwaltet die Chat-Räume, WebSockets, Sitzungen und Authentifizierung.
 */
class ChatManager {
public:
    ChatManager();
    ~ChatManager() = default;

    /**
     * @brief Initialisiert den ChatManager und registriert den WebSocket-Handler am Webserver.
     * @param server Zeiger auf den AsyncWebServer.
     */
    void begin(AsyncWebServer* server);

    /**
     * @brief Führt zyklische Aufräumarbeiten am WebSocket durch.
     */
    void cleanup();

    /**
     * @brief Überprüft, ob ein Client authentifiziert ist.
     */
    bool isClientAuthenticated(AsyncWebSocketClient* client) const;

    /**
     * @brief Gibt den Inaktivitäts-Zeitstempel zurück.
     */
    uint32_t getLastActivityTime() const { return _lastActivity; }

    /**
     * @brief Setzt den Inaktivitäts-Zeitstempel zurück (registriert Aktivität).
     */
    void registerActivity() { _lastActivity = millis(); }

    /**
     * @brief Hilfsfunktion zum Maskieren von HTML-Sonderzeichen (XSS-Schutz).
     */
    static String escapeHtml(const String& s);

private:
    // WebSocket-Objekt für Echtzeitkommunikation
    AsyncWebSocket _ws;

    // Chat-Räume
    MessageRingBuffer _openRoom;
    MessageRingBuffer _lockedRoom;

    // Zeitstempel der letzten Nutzeraktivität
    uint32_t _lastActivity;

    // Generiert eine sichere 4-stellige Hex-ID mithilfe des Hardware-RNG des ESP8266
    String generateSessionId();

    // Berechnet das Authentifizierungs-Token basierend auf Passwort und Salt
    String makeAuthToken();

    // Verarbeitet WebSocket-Ereignisse
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                   AwsTemplateType type, void* arg, uint8_t* data, size_t len);

    // Behandelt eingehende Text-Nachrichten des WebSockets
    void handleWsTextMessage(AsyncWebSocketClient* client, const String& message);

    // Sendet Raum-Initialisierungsdaten an einen bestimmten Client
    void sendRoomInit(AsyncWebSocketClient* client, const String& room);

    // Broadcastet eine Nachricht an alle Clients eines bestimmten Raumes
    void broadcastMessage(const String& room, const String& msg);
};
