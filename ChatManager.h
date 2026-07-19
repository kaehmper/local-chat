#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "MessageRingBuffer.h"
#include "LedManager.h"
#include "OledManager.h"
#include "TicTacToeManager.h"
#include "MeshManager.h"

/**
 * @struct OnlineUser
 * @brief Struktur zur Speicherung von Informationen über online befindliche Benutzer im Mesh.
 */
struct OnlineUser {
    char uid[5];        // 4-stellige Hex-ID + Nullterminierung
    uint32_t lastSeen;  // Zeitstempel der letzten Aktivität
    bool isLocal;       // Gibt an, ob der Benutzer lokal verbunden ist (true) oder über Mesh (false)
};

/**
 * @class ChatManager
 * @brief Koordiniert WebSockets, Online-Listen und verbindet alle Sub-Manager.
 */
class ChatManager {
public:
    ChatManager();
    ~ChatManager() = default;

    /**
     * @brief Initialisiert den ChatManager und alle Sub-Manager. Registers the WS handler on the server.
     */
    void begin(AsyncWebServer* server);

    /**
     * @brief Führt periodische Updates, Aufräumarbeiten und LED-Zustandswechsel durch.
     */
    void update();

    /**
     * @brief Gibt den Inaktivitäts-Zeitstempel zurück.
     */
    uint32_t getLastActivityTime() const { return _lastActivity; }

    /**
     * @brief Ruft eine der letzten drei eindeutigen Ticker-Nachrichten ab.
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

    /**
     * @brief Gibt die Online-Nutzer als formatierte Zeichenkette zurück (z.B. "Users: 1A2B, 3C4D...").
     */
    String getOnlineUsersString();

    /**
     * @brief Gibt die Anzahl der verbundenen Remote-Mesh-Knoten zurück.
     */
    size_t getConnectedNodesCount() { return _meshManager.getConnectedNodesCount(); }

private:
    AsyncWebSocket _ws;
    MessageRingBuffer _openRoom;
    LedManager _ledManager;
    OledManager _oledManager;
    MeshManager _meshManager;

    uint32_t _lastActivity;

    // Ticker-Puffer für die letzten drei eindeutigen Nachrichten
    String _tickerMessages[3];
    size_t _tickerCount;

    // Online-Nutzer-Verwaltung (lokal & mesh-weit)
    static constexpr size_t MAX_ONLINE_USERS = 32;
    OnlineUser _onlineUsers[MAX_ONLINE_USERS];
    size_t _onlineUsersCount;
    uint32_t _lastUserListBroadcastTime;

    void updateOnlineUsersList();
    void broadcastUserList();
    void addOrUpdateUser(const char* uid, bool isLocal);
    void handleTttMessage(AsyncWebSocketClient* client, const String& message);

    // Prüft, ob eine UID bereits von einem lokalen Client oder im Mesh besetzt ist
    bool isUidInUse(const String& uid);

    // Generiert eine sichere 4-stellige Hex-ID mithilfe des Hardware-RNG des ESP8266
    String generateSessionId();

    // Verarbeitet WebSocket-Ereignisse
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);

    // Behandelt eingehende Text-Nachrichten des WebSockets
    void handleWsTextMessage(AsyncWebSocketClient* client, const String& message);

    // Sendet Raum-Initialisierungsdaten an einen bestimmten Client
    void sendRoomInit(AsyncWebSocketClient* client);

    // Broadcastet eine Nachricht an alle Clients des Raumes
    void broadcastMessage(const String& msg);

    // Callback-Handler für ESP-NOW Mesh-Ereignisse
    void handleMeshIncomingMsg(const String& msg);
    void handleMeshSyncEnd();
    void handleMeshUserPing(const String& payload);
    void handleMeshTttMsg(const String& payload);
};
