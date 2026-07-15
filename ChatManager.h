#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "Config.h"

#if ENABLE_MESH
#include <espnow.h>

#pragma pack(push, 1)
struct MeshPacket {
    uint8_t packetType;       // 1 = CHAT_MSG, 2 = SYNC_REQ, 3 = SYNC_MSG, 4 = SYNC_END
    uint32_t senderId;        // Unique node sender ID
    char message[201];        // Text message payload
};
#pragma pack(pop)

#endif

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
    const char* get(size_t index) const;

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
     * @brief Führt zyklische Aufräumarbeiten am WebSocket und Mesh durch.
     */
    void cleanup();

    /**
     * @brief Gibt den Inaktivitäts-Zeitstempel zurück.
     */
    uint32_t getLastActivityTime() const { return _lastActivity; }

    /**
     * @brief Ruft eine der letzten drei eindeutigen Nachrichten ab (0 = neueste, 1 = zweitneueste, 2 = drittneueste).
     */
    String getLastTickerMessage(size_t index) const;

    /**
     * @brief Gibt die Anzahl der aktuell im Ticker-Puffer gespeicherten Nachrichten zurück (0 bis 3).
     */
    size_t getTickerMessageCount() const { return _tickerCount; }

    /**
     * @brief Fügt eine Nachricht dem Ticker-Puffer hinzu.
     */
    void addTickerMessage(const String& msg);

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

    // Chat-Raum
    MessageRingBuffer _openRoom;

    // Zeitstempel der letzten Nutzeraktivität
    uint32_t _lastActivity;

    // Ticker-Puffer für die letzten drei eindeutigen Nachrichten
    String _tickerMessages[3];
    size_t _tickerCount;

    // Generiert eine sichere 4-stellige Hex-ID mithilfe des Hardware-RNG des ESP8266
    String generateSessionId();

    // Berechnet das Authentifizierungs-Token basierend auf Passwort und Salt
    String makeAuthToken();

    // Verarbeitet WebSocket-Ereignisse
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);

    // Behandelt eingehende Text-Nachrichten des WebSockets
    void handleWsTextMessage(AsyncWebSocketClient* client, const String& message);

    // Sendet Raum-Initialisierungsdaten an einen bestimmten Client
    void sendRoomInit(AsyncWebSocketClient* client);

    // Broadcastet eine Nachricht an alle Clients des Raumes
    void broadcastMessage(const String& msg);

#if ENABLE_MESH
public:
    // Statischer Callback für ESP-NOW Empfang
    static void onEspNowRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len);

private:
    uint32_t _nodeId;
    uint32_t _lastPingTime;

    // Non-blocking Defer-Schnittstelle für Verlaufs-Synchronisierung
    bool _syncInProgress;
    size_t _syncNextIndex;
    uint32_t _lastSyncMsgTime;

    void initMesh();
    void sendMeshBroadcast(uint8_t packetType, const String& msg);
    void handleIncomingPacket(const MeshPacket& packet);
    void handleSyncRequest(uint32_t targetNodeId);
    void handleSyncResponse(const MeshPacket& packet);
    void handleUserMeshPing(const String& payload);
    void handleMeshTttMessage(const String& payload);
#endif
};
