#include "MeshManager.h"
#include <cstring>

#if ENABLE_MESH
// Globaler Zeiger auf die aktive Instanz für den statischen ESP-NOW Callback
static MeshManager* g_meshManagerInstance = nullptr;
#endif

MeshManager::MeshManager()
    : _nodeId(0),
      _lastPingTime(0),
      _syncInProgress(false),
      _syncNextIndex(0),
      _lastSyncMsgTime(0),
      _remoteNodesCount(0) {
    std::memset(_remoteNodes, 0, sizeof(_remoteNodes));
}

void MeshManager::begin(MessageReceivedCallback onMsg,
                        SyncEndCallback onSyncEnd,
                        UserPingReceivedCallback onUserPing,
                        TttMsgReceivedCallback onTttMsg) {
    _onMsg = onMsg;
    _onSyncEnd = onSyncEnd;
    _onUserPing = onUserPing;
    _onTttMsg = onTttMsg;

#if ENABLE_MESH
    g_meshManagerInstance = this;
    _nodeId = ESP.getChipId();
    _lastPingTime = millis();

    // Initialisiere das ESP-NOW SDK
    if (esp_now_init() != 0) {
        Serial.println("[ESP-NOW] Fehler bei der Initialisierung");
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);

    // Broadcast-Peer registrieren (MAC: FF:FF:FF:FF:FF:FF)
    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, Config::MESH_CHANNEL, NULL, 0);

    // Empfangs-Callback registrieren
    esp_now_register_recv_cb(MeshManager::onEspNowRecv);

    Serial.println("[ESP-NOW] Erfolgreich initialisiert auf Node ID: " + String(_nodeId));

    // Nach dem Start einen Sync-Request broadcasten, um Nachbarn zu finden und Historie zu holen
    sendBroadcast(2, ""); // Type 2: SYNC_REQ
#endif
}

void MeshManager::update(const std::function<String(size_t)>& getMessageByIndex, size_t messageCount) {
    uint32_t now = millis();

#if ENABLE_MESH
    // Periodische Bereinigung abgelaufener Remote-Knoten (> 10 Sekunden)
    size_t writeIdx = 0;
    for (size_t i = 0; i < _remoteNodesCount; ++i) {
        if (now - _remoteNodes[i].lastSeen < 10000) {
            _remoteNodes[writeIdx++] = _remoteNodes[i];
        }
    }
    _remoteNodesCount = writeIdx;

    // Alle 60 Sekunden periodischer Sync-Request
    if (now - _lastPingTime > 60000) {
        _lastPingTime = now;
        sendBroadcast(2, ""); // SYNC_REQ senden
    }

    // Non-blocking segmentierte Verlaufsübertragung (History Sync)
    if (_syncInProgress) {
        if (now - _lastSyncMsgTime >= 15) { // 15ms non-blocking delay zwischen Nachrichten
            _lastSyncMsgTime = now;
            if (_syncNextIndex < messageCount) {
                sendBroadcast(3, getMessageByIndex(_syncNextIndex)); // Type 3 (SYNC_MSG)
                _syncNextIndex++;
            } else {
                sendBroadcast(4, ""); // Type 4: SYNC_END
                _syncInProgress = false;
                Serial.println("[ESP-NOW] Verlaufs-Übertragung abgeschlossen.");
            }
        }
    }
#endif
}

void MeshManager::sendBroadcast(uint8_t packetType, const String& msg) {
#if ENABLE_MESH
    MeshPacket packet;
    packet.packetType = packetType;
    packet.senderId = _nodeId;

    std::memset(packet.message, 0, sizeof(packet.message));
    std::strncpy(packet.message, msg.c_str(), sizeof(packet.message) - 1);

    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcastMac, reinterpret_cast<uint8_t*>(&packet), sizeof(MeshPacket));
#endif
}

size_t MeshManager::getConnectedNodesCount() {
    uint32_t now = millis();
    size_t activeCount = 0;
    for (size_t i = 0; i < _remoteNodesCount; ++i) {
        if (now - _remoteNodes[i].lastSeen < 10000) {
            activeCount++;
        }
    }
    return activeCount;
}

void MeshManager::registerRemoteNode(uint32_t nodeId) {
    if (nodeId == 0 || nodeId == _nodeId) return;

    uint32_t now = millis();

    // 1. Abgelaufene Knoten bereinigen
    size_t writeIdx = 0;
    for (size_t i = 0; i < _remoteNodesCount; ++i) {
        if (now - _remoteNodes[i].lastSeen < 10000) {
            _remoteNodes[writeIdx++] = _remoteNodes[i];
        }
    }
    _remoteNodesCount = writeIdx;

    // 2. Knoten suchen und aktualisieren oder neu anlegen
    bool found = false;
    for (size_t i = 0; i < _remoteNodesCount; ++i) {
        if (_remoteNodes[i].nodeId == nodeId) {
            _remoteNodes[i].lastSeen = now;
            found = true;
            break;
        }
    }

    if (!found && _remoteNodesCount < MAX_REMOTE_NODES) {
        _remoteNodes[_remoteNodesCount].nodeId = nodeId;
        _remoteNodes[_remoteNodesCount].lastSeen = now;
        _remoteNodesCount++;
    }
}

#if ENABLE_MESH
void MeshManager::onEspNowRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
    if (g_meshManagerInstance && len == sizeof(MeshPacket)) {
        g_meshManagerInstance->handleIncomingPacket(incomingData, len);
    }
}

void MeshManager::handleIncomingPacket(const uint8_t* data, size_t len) {
    MeshPacket packet;
    std::memcpy(&packet, data, sizeof(MeshPacket));

    if (packet.senderId == _nodeId) {
        return;
    }

    registerRemoteNode(packet.senderId);

    // Stack-basierter Puffer für die sichere Nullterminierung (verhindert Heap-Bound Over-Reads)
    char safeMessage[sizeof(packet.message)];
    std::memcpy(safeMessage, packet.message, sizeof(packet.message));
    safeMessage[sizeof(packet.message) - 1] = '\0';

    String msgPayload = String(safeMessage);

    if (packet.packetType == 1) { // CHAT_MSG
        if (_onMsg) {
            _onMsg(msgPayload);
        }
    }
    else if (packet.packetType == 2) { // SYNC_REQ
        handleSyncRequest(packet.senderId);
    }
    else if (packet.packetType == 3) { // SYNC_MSG
        handleSyncResponse(msgPayload);
    }
    else if (packet.packetType == 4) { // SYNC_END
        if (_onSyncEnd) {
            _onSyncEnd();
        }
    }
    else if (packet.packetType == 5) { // USER_PING
        if (_onUserPing) {
            _onUserPing(msgPayload);
        }
    }
    else if (packet.packetType == 6) { // TTT_MSG
        if (_onTttMsg) {
            _onTttMsg(msgPayload);
        }
    }
}

void MeshManager::handleSyncRequest(uint32_t targetNodeId) {
    Serial.println("[ESP-NOW] Verlaufs-Anforderung von Node " + String(targetNodeId) + " empfangen. Starte non-blocking Sync...");
    _syncInProgress = true;
    _syncNextIndex = 0;
    _lastSyncMsgTime = millis();
}

void MeshManager::handleSyncResponse(const String& payload) {
    if (_onMsg) {
        _onMsg(payload);
    }
}
#endif
