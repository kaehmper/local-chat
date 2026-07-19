#pragma once

#include <Arduino.h>
#include <functional>
#include "config.h"

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
 * @class MeshManager
 * @brief Verwaltet die ESP-NOW Mesh-Kommunikation und Synchronisierung.
 *
 * Ermöglicht den Austausch von Echtzeit-Nachrichten, die Synchronisierung der Historie
 * und das Weiterleiten von Tic-Tac-Toe Minigame-Zügen.
 */
class MeshManager {
public:
    using MessageReceivedCallback = std::function<void(const String& msg)>;
    using SyncEndCallback = std::function<void()>;
    using UserPingReceivedCallback = std::function<void(const String& payload)>;
    using TttMsgReceivedCallback = std::function<void(const String& payload)>;

    MeshManager();

    /**
     * @brief Initialisiert das ESP-NOW-Mesh und registriert Empfangs-Callbacks.
     */
    void begin(MessageReceivedCallback onMsg,
               SyncEndCallback onSyncEnd,
               UserPingReceivedCallback onUserPing,
               TttMsgReceivedCallback onTttMsg);

    /**
     * @brief Verarbeitet periodische Bereinigungen und den non-blocking Verlaufssync.
     * @param getMessageByIndex Eine Funktion, um historische Nachrichten abzurufen.
     * @param messageCount Die Gesamtzahl der historischen Nachrichten.
     */
    void update(const std::function<String(size_t)>& getMessageByIndex, size_t messageCount);

    /**
     * @brief Sendet einen Broadcast an alle Mesh-Knoten.
     * @param packetType Typ des Pakets (1 = MSG, 2 = SYNC_REQ, 3 = SYNC_MSG, 4 = SYNC_END, 5 = USER_PING, 6 = TTT_MSG)
     * @param msg Text-Payload.
     */
    void sendBroadcast(uint8_t packetType, const String& msg);

    /**
     * @brief Gibt die Anzahl der aktuell erreichbaren Remote-Mesh-Knoten zurück.
     */
    size_t getConnectedNodesCount();

    /**
     * @brief Gibt die eigene eindeutige Node-ID zurück.
     */
    uint32_t getNodeId() const { return _nodeId; }

    /**
     * @brief Registriert die Aktivität eines Remote-Knotens.
     */
    void registerRemoteNode(uint32_t nodeId);

    /**
     * @brief Gibt die Node-ID eines bestimmten remote Knotens zurück.
     */
    uint32_t getRemoteNodeId(size_t index) const {
        if (index < _remoteNodesCount) return _remoteNodes[index].nodeId;
        return 0;
    }

    /**
     * @brief Gibt den RSSI-Wert eines bestimmten remote Knotens zurück.
     */
    int getRemoteNodeRssi(size_t index) const {
        if (index < _remoteNodesCount) return _remoteNodes[index].rssi;
        return -127;
    }

    /**
     * @brief Gibt den RSSI-Wert des stärksten verbundenen remote Knotens zurück.
     */
    int getStrongestNodeRssi();

#if ENABLE_MESH
    // Statischer Callback für ESP-NOW Empfang
    static void onEspNowRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len);
#endif

private:
    uint32_t _nodeId;
    uint32_t _lastPingTime;

    // Callbacks
    MessageReceivedCallback _onMsg;
    SyncEndCallback _onSyncEnd;
    UserPingReceivedCallback _onUserPing;
    TttMsgReceivedCallback _onTttMsg;

    // Segmentierte Verlaufsübertragung (non-blocking)
    bool _syncInProgress;
    size_t _syncNextIndex;
    uint32_t _lastSyncMsgTime;

    // Aktive Knoten-Verfolgung
    struct ActiveNode {
        uint32_t nodeId;
        uint32_t lastSeen;
        int rssi; // Signalstärke in dBm
    };
    static constexpr size_t MAX_REMOTE_NODES = 32;
    ActiveNode _remoteNodes[MAX_REMOTE_NODES];
    size_t _remoteNodesCount;

    void handleIncomingPacket(const uint8_t* data, size_t len);
    void handleSyncRequest(uint32_t targetNodeId);
    void handleSyncResponse(const String& payload);
};
