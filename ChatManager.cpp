#include "ChatManager.h"
#include <cstring>

// ---------- ClientSession Struktur ----------
struct ClientSession {
    String uid;
    String wsBuffer;
};

// Globaler externer Zeiger auf ChatManager (für statische ESP-NOW Callbacks)
extern ChatManager chatManager;

// Hilfsfunktionen zur versionsunabhängigen Ermittlung des Zeigers auf einen Client
static inline AsyncWebSocketClient* getClientPtr(AsyncWebSocketClient* p) { return p; }
static inline AsyncWebSocketClient* getClientPtr(AsyncWebSocketClient& r) { return &r; }

// ==================== MessageRingBuffer ====================

MessageRingBuffer::MessageRingBuffer() : _start(0), _count(0) {
    std::memset(_buffer, 0, sizeof(_buffer));
}

void MessageRingBuffer::add(const String& msg) {
    size_t idx = (_start + _count) % Config::MAX_MESSAGES;
    std::strncpy(_buffer[idx], msg.c_str(), Config::MAX_MSG_LENGTH);
    _buffer[idx][Config::MAX_MSG_LENGTH] = '\0'; // Nullterminierung sichern

    if (_count < Config::MAX_MESSAGES) {
        _count++;
    } else {
        _start = (_start + 1) % Config::MAX_MESSAGES;
    }
}

size_t MessageRingBuffer::size() const {
    return _count;
}

const char* MessageRingBuffer::get(size_t index) const {
    if (index >= _count) return "";
    size_t idx = (_start + index) % Config::MAX_MESSAGES;
    return _buffer[idx];
}


// ==================== ChatManager ====================

ChatManager::ChatManager()
    : _ws("/ws"), _lastActivity(0), _tickerCount(0), _onlineUsersCount(0), _lastUserListBroadcastTime(0) {
#if ENABLE_MESH
    _nodeId = 0;
    _lastPingTime = 0;
    _syncInProgress = false;
    _syncNextIndex = 0;
    _lastSyncMsgTime = 0;
#endif
}

void ChatManager::begin(AsyncWebServer* server) {
    _lastActivity = millis();

    // WebSocket-Event-Callback registrieren mit versionskompatiblem AwsEventType
    _ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                       AwsEventType t, void* arg, uint8_t* d, size_t l) {
        this->onWsEvent(s, c, t, arg, d, l);
    });

    server->addHandler(&_ws);

#if ENABLE_MESH
    initMesh();
#endif
}

void ChatManager::cleanup() {
    _ws.cleanupClients();

    uint32_t now = millis();
    if (now - _lastUserListBroadcastTime > 4000) {
        _lastUserListBroadcastTime = now;
        broadcastUserList();
    }

#if ENABLE_MESH
    if (now - _lastPingTime > 60000) { // Alle 60 Sekunden periodischer Sync-Request
        _lastPingTime = now;
        sendMeshBroadcast(2, ""); // SYNC_REQ senden
    }

    // Wenn eine Verlaufsübertragung (Sync-Request) ansteht, wickeln wir sie häppchenweise non-blocking hier ab
    if (_syncInProgress) {
        if (now - _lastSyncMsgTime >= 15) { // 15ms non-blocking delay zwischen Nachrichten
            _lastSyncMsgTime = now;
            size_t openCount = _openRoom.size();
            if (_syncNextIndex < openCount) {
                sendMeshBroadcast(3, _openRoom.get(_syncNextIndex)); // Type 3 (SYNC_MSG)
                _syncNextIndex++;
            } else {
                // Alle Nachrichten übertragen. Sende Sync-Ende
                sendMeshBroadcast(4, ""); // Type 4: SYNC_END
                _syncInProgress = false;
                Serial.println("[ESP-NOW] Verlaufs-Übertragung abgeschlossen.");
            }
        }
    }
#endif
}

String ChatManager::escapeHtml(const String& s) {
    String out;
    out.reserve(s.length() + 10);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#x27;"; break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            default:   out += c;
        }
    }
    return out;
}

bool ChatManager::isUidInUse(const String& uid) {
    if (uid.length() != 4) return false;

    // 1. Lokale WebSocket-Clients prüfen
    for (auto&& client_item : _ws.getClients()) {
        AsyncWebSocketClient* client = getClientPtr(client_item);
        if (client && client->status() == WS_CONNECTED) {
            auto* session = static_cast<ClientSession*>(client->_tempObject);
            if (session && session->uid.equalsIgnoreCase(uid)) {
                return true;
            }
        }
    }

    // 2. Bekannte Online-Nutzerliste (auch remote) prüfen
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (uid.equalsIgnoreCase(_onlineUsers[i].uid)) {
            return true;
        }
    }

    return false;
}

String ChatManager::generateSessionId() {
    char buf[5];
    String uid;
    // Max. 20 Versuche, um eine kollisionsfreie UID zu finden
    for (int attempts = 0; attempts < 20; ++attempts) {
        uint16_t r = static_cast<uint16_t>(ESP.random() & 0xFFFF);
        std::sprintf(buf, "%04X", r);
        uid = String(buf);
        if (!isUidInUse(uid)) {
            return uid;
        }
    }
    return uid;
}

void ChatManager::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                            AwsEventType type, void* arg, uint8_t* data, size_t len) {
    _lastActivity = millis();

    switch (type) {
        case WS_EVT_CONNECT: {
            String uid = generateSessionId();
            client->_tempObject = new ClientSession{uid, ""};
            break;
        }
        case WS_EVT_DISCONNECT: {
            auto* session = static_cast<ClientSession*>(client->_tempObject);
            if (session) {
                delete session;
                client->_tempObject = nullptr;
            }
            break;
        }
        case WS_EVT_DATA: {
            AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
            if (info->opcode == WS_TEXT) {
                auto* session = static_cast<ClientSession*>(client->_tempObject);
                if (session) {
                    if (info->index == 0) {
                        session->wsBuffer = "";
                        session->wsBuffer.reserve(info->len);
                    }

                    for (size_t i = 0; i < len; i++) {
                        session->wsBuffer += static_cast<char>(data[i]);
                    }

                    if (info->index + len >= info->len) {
                        handleWsTextMessage(client, session->wsBuffer);
                        session->wsBuffer = ""; // Speicher freigeben
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

static String unescapeJsonString(const String& val) {
    String result;
    result.reserve(val.length());
    for (size_t i = 0; i < val.length(); i++) {
        if (val[i] == '\\' && i + 1 < val.length()) {
            char next = val[i+1];
            switch (next) {
                case '"':  result += '"';  i++; break;
                case '\\': result += '\\'; i++; break;
                case '/':  result += '/';  i++; break;
                case 'b':  result += '\b'; i++; break;
                case 'f':  result += '\f'; i++; break;
                case 'n':  result += '\n'; i++; break;
                case 'r':  result += '\r'; i++; break;
                case 't':  result += '\t'; i++; break;
                default:   result += '\\'; break; // Unbekannte Sequenz: Backslash behalten
            }
        } else {
            result += val[i];
        }
    }
    return result;
}

// Hilfsfunktion zum Parsen einfacher JSON-Schlüssel (Vermeidet Abhängigkeit von ArduinoJson)
static String getJsonValue(const String& json, const String& key) {
    // 1. String-Typ suchen: "key":"value"
    String searchKey = "\"" + key + "\":\"";
    int start = json.indexOf(searchKey);
    if (start != -1) {
        start += searchKey.length();

        // Finde das korrekte schließende, nicht-escapte Anführungszeichen
        int end = -1;
        int curr = start;
        while (curr < (int)json.length()) {
            int nextQuote = json.indexOf('"', curr);
            if (nextQuote == -1) break;

            // Backslashes davor zählen
            int backslashes = 0;
            int bsIndex = nextQuote - 1;
            while (bsIndex >= start && json[bsIndex] == '\\') {
                backslashes++;
                bsIndex--;
            }
            if (backslashes % 2 == 0) {
                end = nextQuote;
                break;
            }
            curr = nextQuote + 1;
        }

        if (end != -1) {
            return unescapeJsonString(json.substring(start, end));
        }
    } else {
        // 2. Andere Typen (Zahl, Boolean): "key":value
        searchKey = "\"" + key + "\":";
        start = json.indexOf(searchKey);
        if (start != -1) {
            start += searchKey.length();
            int endCom = json.indexOf(",", start);
            int endObj = json.indexOf("}", start);
            int end = -1;
            if (endCom != -1 && endObj != -1) {
                end = (endCom < endObj) ? endCom : endObj;
            } else if (endCom != -1) {
                end = endCom;
            } else if (endObj != -1) {
                end = endObj;
            }
            if (end != -1) {
                String val = json.substring(start, end);
                val.replace("\"", "");
                val.trim();
                return val;
            } else {
                // Falls weder Komma noch schließende Klammer existieren, nimm den Rest des Strings
                String val = json.substring(start);
                val.replace("\"", "");
                val.trim();
                return val;
            }
        }
    }
    return "";
}

void ChatManager::handleWsTextMessage(AsyncWebSocketClient* client, const String& message) {
    auto* session = static_cast<ClientSession*>(client->_tempObject);
    if (!session) return;

    String type = getJsonValue(message, "type");

    if (type == "init") {
        String requestedUid = getJsonValue(message, "uid");
        // Validiere die angeforderte UID: Muss exakt aus 4 Hexadezimalzeichen bestehen
        bool isValid = (requestedUid.length() == 4);
        if (isValid) {
            for (int i = 0; i < 4; ++i) {
                char c = requestedUid[i];
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
                    isValid = false;
                    break;
                }
            }
        }

        // Falls die angeforderte UID gültig ist UND noch nicht besetzt ist, übernehmen wir sie.
        // Andernfalls weisen wir eine neu generierte, eindeutige UID zu.
        if (isValid && !isUidInUse(requestedUid)) {
            session->uid = requestedUid;
            session->uid.toUpperCase(); // In Großbuchstaben vereinheitlichen
            Serial.println("[Session] Vorhandene UID übernommen: " + session->uid);
        } else {
            // Falls bereits besetzt, verwerfen wir sie und weisen eine neue eindeutige UID zu
            if (isValid && isUidInUse(requestedUid)) {
                Serial.println("[Session] Angeforderte UID bereits vergeben: " + requestedUid + ", generiere neue UID.");
            }
            session->uid = generateSessionId();
        }

        // Füge den User sofort zur Online-Liste hinzu und sende ein Update
        addOrUpdateUser(session->uid.c_str(), true);
        broadcastUserList();

        sendRoomInit(client);
    }
    else if (type == "ttt") {
        handleTttMessage(client, message);
    }
    else if (type == "post") {
        String rawText = getJsonValue(message, "text");
        String cleanText = escapeHtml(rawText);
        if (cleanText.length() > Config::MAX_MSG_LENGTH) {
            cleanText = cleanText.substring(0, Config::MAX_MSG_LENGTH);
        }

        if (cleanText.length() > 0) {
            String formattedMsg = "[" + session->uid + "] " + cleanText;
            _openRoom.add(formattedMsg);
            addTickerMessage(formattedMsg);
            broadcastMessage(formattedMsg);

#if ENABLE_MESH
            // Übertragungs-Eigenschaften für Echtzeit-Mesh-Nachrichten
            sendMeshBroadcast(1, formattedMsg); // Type 1: CHAT_MSG
#endif
        }
    }
}

static String escapeJsonStringValue(const String& val) {
    String escaped;
    escaped.reserve(val.length() + 8);
    for (char c : val) {
        switch (c) {
            case '\\': escaped += "\\\\"; break;
            case '"':  escaped += "\\\""; break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            case '\b': escaped += "\\b";  break;
            case '\f': escaped += "\\f";  break;
            default:   escaped += c;      break;
        }
    }
    return escaped;
}

void ChatManager::sendRoomInit(AsyncWebSocketClient* client) {
    auto* session = static_cast<ClientSession*>(client->_tempObject);
    if (!session) return;

    const MessageRingBuffer& buffer = _openRoom;
    size_t count = buffer.size();

    // Exakten Speicherbedarf vorab berechnen, um Heap-Fragmentierung zu verhindern
    size_t requiredSize = 128 + session->uid.length();
    for (size_t i = 0; i < count; ++i) {
        requiredSize += std::strlen(buffer.get(i)) + 8;
    }

    String json;
    json.reserve(requiredSize);

    json += "{\"type\":\"init\",";
    json += "\"uid\":\"" + session->uid + "\",";
    json += "\"messages\":[";

    for (size_t i = 0; i < count; ++i) {
        json += "\"" + escapeJsonStringValue(buffer.get(i)) + "\"";
        if (i < count - 1) {
            json += ",";
        }
    }
    json += "]}";

    client->text(json);
}

void ChatManager::broadcastMessage(const String& msg) {
    String escapedMsg = escapeJsonStringValue(msg);

    String json;
    json.reserve(64 + escapedMsg.length());
    json += "{\"type\":\"msg\",\"msg\":\"" + escapedMsg + "\"}";

    for (auto&& client_item : _ws.getClients()) {
        AsyncWebSocketClient* client = getClientPtr(client_item);
        if (client && client->status() == WS_CONNECTED) {
            // Verbindung bei stockender Warteschlange trennen, um Speicher zu schonen
            if (client->queueLen() > 4) {
                client->close();
                continue;
            }
            auto* session = static_cast<ClientSession*>(client->_tempObject);
            if (session) {
                client->text(json);
            }
        }
    }
}

void ChatManager::addTickerMessage(const String& msg) {
    if (msg.length() == 0) return;

    // Dubletten-Prüfung innerhalb des Ticker-Puffers, um redundante Einträge zu vermeiden
    for (size_t i = 0; i < _tickerCount; ++i) {
        if (_tickerMessages[i].equals(msg)) {
            // Falls das Element bereits existiert, verschieben wir es an die Spitze (Index 0)
            // und rücken die davor liegenden Elemente entsprechend nach hinten
            for (size_t j = i; j > 0; --j) {
                _tickerMessages[j] = _tickerMessages[j - 1];
            }
            _tickerMessages[0] = msg;
            return;
        }
    }

    // Wenn es neu ist, schieben wir alle Elemente um 1 Position nach hinten
    _tickerMessages[2] = _tickerMessages[1];
    _tickerMessages[1] = _tickerMessages[0];
    _tickerMessages[0] = msg;

    if (_tickerCount < 3) {
        _tickerCount++;
    }
}

String ChatManager::getLastTickerMessage(size_t index) const {
    if (index >= _tickerCount) return "";
    return _tickerMessages[index];
}

void ChatManager::addOrUpdateUser(const char* uid, bool isLocal) {
    if (!uid || std::strlen(uid) != 4) return;

    uint32_t now = millis();

    // Suchen, ob der Benutzer bereits vorhanden ist
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (std::strcmp(_onlineUsers[i].uid, uid) == 0) {
            _onlineUsers[i].lastSeen = now;
            _onlineUsers[i].isLocal = isLocal;
            return;
        }
    }

    // Wenn nicht vorhanden, neu hinzufügen (falls Platz ist)
    if (_onlineUsersCount < MAX_ONLINE_USERS) {
        std::strncpy(_onlineUsers[_onlineUsersCount].uid, uid, 4);
        _onlineUsers[_onlineUsersCount].uid[4] = '\0';
        _onlineUsers[_onlineUsersCount].lastSeen = now;
        _onlineUsers[_onlineUsersCount].isLocal = isLocal;
        _onlineUsersCount++;
    }
}

String ChatManager::getOnlineUsersString() {
    updateOnlineUsersList();
    if (_onlineUsersCount == 0) {
        return "Users: none";
    }
    String usersStr = "Users: ";
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        usersStr += _onlineUsers[i].uid;
        if (i < _onlineUsersCount - 1) {
            usersStr += ", ";
        }
    }
    return usersStr;
}

void ChatManager::updateOnlineUsersList() {
    uint32_t now = millis();

    // 1. Behalte Remote-User, die noch nicht abgelaufen sind (jünger als 15 Sekunden)
    size_t writeIdx = 0;
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (!_onlineUsers[i].isLocal) {
            if (now - _onlineUsers[i].lastSeen < 15000) {
                _onlineUsers[writeIdx++] = _onlineUsers[i];
            }
        }
    }
    _onlineUsersCount = writeIdx;

    // 2. Füge alle aktuell aktiv verbundenen WebSocket-Nutzer wieder als lokal hinzu
    for (auto&& client_item : _ws.getClients()) {
        AsyncWebSocketClient* client = getClientPtr(client_item);
        if (client && client->status() == WS_CONNECTED) {
            auto* session = static_cast<ClientSession*>(client->_tempObject);
            if (session && session->uid.length() == 4) {
                addOrUpdateUser(session->uid.c_str(), true);
            }
        }
    }
}

void ChatManager::broadcastUserList() {
    updateOnlineUsersList();

    // 1. Lokale Benutzer sammeln, um sie an andere Nodes via ESP-NOW zu senden
    String localUids = "";
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (_onlineUsers[i].isLocal) {
            if (localUids.length() > 0) localUids += ",";
            localUids += _onlineUsers[i].uid;
        }
    }

#if ENABLE_MESH
    // Sende lokalen User-Ping an andere Mesh-Knoten (Type 5)
    if (localUids.length() > 0) {
        sendMeshBroadcast(5, localUids);
    }
#endif

    // 2. Erstelle JSON mit ALLEN aktiven Online-Nutzern und sende es an alle WebSockets
    String json;
    json.reserve(256);
    json += "{\"type\":\"users\",\"list\":[";
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        json += "\"" + String(_onlineUsers[i].uid) + "\"";
        if (i < _onlineUsersCount - 1) {
            json += ",";
        }
    }
    json += "]}";

    for (auto&& client_item : _ws.getClients()) {
        AsyncWebSocketClient* client = getClientPtr(client_item);
        if (client && client->status() == WS_CONNECTED) {
            auto* session = static_cast<ClientSession*>(client->_tempObject);
            if (session) {
                client->text(json);
            }
        }
    }
}

void ChatManager::handleTttMessage(AsyncWebSocketClient* client, const String& message) {
    auto* session = static_cast<ClientSession*>(client->_tempObject);
    if (!session) return;

    String targetUid = getJsonValue(message, "to");
    if (targetUid.length() != 4) return;

    targetUid.toUpperCase();

    // 1. Prüfen, ob der Ziel-User lokal an diesem Node verbunden ist
    bool foundLocally = false;
    for (auto&& client_item : _ws.getClients()) {
        AsyncWebSocketClient* localClient = getClientPtr(client_item);
        if (localClient && localClient->status() == WS_CONNECTED) {
            auto* s = static_cast<ClientSession*>(localClient->_tempObject);
            if (s && s->uid == targetUid) {
                localClient->text(message);
                foundLocally = true;
                break;
            }
        }
    }

#if ENABLE_MESH
    // 2. Wenn nicht lokal gefunden, via ESP-NOW Mesh broadcasten
    if (!foundLocally) {
        sendMeshBroadcast(6, message);
    }
#endif
}

// ==================== LIGHTWEIGHT ESP-NOW MESH BACKHAUL ====================
#if ENABLE_MESH

void ChatManager::initMesh() {
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
    esp_now_register_recv_cb(ChatManager::onEspNowRecv);

    Serial.println("[ESP-NOW] Erfolgreich initialisiert auf Node ID: " + String(_nodeId));

    // Nach dem Start einen Sync-Request broadcasten, um Nachbarn zu finden und Historie zu holen
    sendMeshBroadcast(2, ""); // Type 2: SYNC_REQ
}

void ChatManager::onEspNowRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
    if (len == sizeof(MeshPacket)) {
        MeshPacket packet;
        std::memcpy(&packet, incomingData, sizeof(MeshPacket));
        chatManager.handleIncomingPacket(packet);
    }
}

void ChatManager::handleIncomingPacket(const MeshPacket& packet) {
    // Eigene Pakete ignorieren
    if (packet.senderId == _nodeId) {
        return;
    }

    _lastActivity = millis(); // System-Aktivität registrieren

    // Sicherstellen, dass das empfangene Nachrichtenfeld im Stack-Objekt nullterminiert ist (Verhinderung von Buffer Over-reads)
    char safeMessage[sizeof(packet.message)];
    std::memcpy(safeMessage, packet.message, sizeof(packet.message));
    safeMessage[sizeof(packet.message) - 1] = '\0';

    String msgPayload = String(safeMessage);

    if (packet.packetType == 1) { // 1 = CHAT_MSG (Echtzeit-Nachricht)
        MessageRingBuffer& targetRoom = _openRoom;

        // Dubletten-Prüfung vor dem Einfügen
        bool exists = false;
        size_t count = targetRoom.size();
        for (size_t i = 0; i < count; ++i) {
            if (msgPayload.equals(targetRoom.get(i))) {
                exists = true;
                break;
            }
        }

        if (!exists && msgPayload.length() > 0) {
            targetRoom.add(msgPayload);
            addTickerMessage(msgPayload);
            // Lokal an alle WebSocket-Clients senden
            broadcastMessage(msgPayload);
        }
    }
    else if (packet.packetType == 2) { // 2 = SYNC_REQ (Historie angefordert)
        handleSyncRequest(packet.senderId);
    }
    else if (packet.packetType == 3) { // 3 = SYNC_MSG (Historien-Nachricht)
        handleSyncResponse(packet);
    }
    else if (packet.packetType == 4) { // 4 = SYNC_END (Historie-Ende erreicht)
        Serial.println("[ESP-NOW] Historien-Synchronisierung abgeschlossen. Aktualisiere Clients...");
        // Re-Initialisiere alle lokalen WebSockets, damit die Historie sichtbar ist
        for (auto&& client_item : _ws.getClients()) {
            AsyncWebSocketClient* client = getClientPtr(client_item);
            if (client && client->status() == WS_CONNECTED) {
                auto* session = static_cast<ClientSession*>(client->_tempObject);
                if (session) {
                    sendRoomInit(client);
                }
            }
        }
    }
    else if (packet.packetType == 5) { // 5 = USER_PING (Remote online users)
        handleUserMeshPing(msgPayload);
    }
    else if (packet.packetType == 6) { // 6 = TTT_MSG (Tic-Tac-Toe Spielereignis)
        handleMeshTttMessage(msgPayload);
    }
}

void ChatManager::sendMeshBroadcast(uint8_t packetType, const String& msg) {
    MeshPacket packet;
    packet.packetType = packetType;
    packet.senderId = _nodeId;

    std::memset(packet.message, 0, sizeof(packet.message));
    std::strncpy(packet.message, msg.c_str(), sizeof(packet.message) - 1);

    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcastMac, reinterpret_cast<uint8_t*>(&packet), sizeof(MeshPacket));
}

void ChatManager::handleSyncRequest(uint32_t targetNodeId) {
    Serial.println("[ESP-NOW] Verlaufs-Anforderung von Node " + String(targetNodeId) + " empfangen. Starte non-blocking Sync...");

    // Initiieren der non-blocking segmentierten Übertragung im main loop (cleanup)
    _syncInProgress = true;
    _syncNextIndex = 0;
    _lastSyncMsgTime = millis();
}

void ChatManager::handleSyncResponse(const MeshPacket& packet) {
    // Sicherstellen, dass das empfangene Nachrichtenfeld im Stack-Objekt nullterminiert ist (Verhinderung von Buffer Over-reads)
    char safeMessage[sizeof(packet.message)];
    std::memcpy(safeMessage, packet.message, sizeof(packet.message));
    safeMessage[sizeof(packet.message) - 1] = '\0';

    String msgPayload = String(safeMessage);
    MessageRingBuffer& targetRoom = _openRoom;

    // Dubletten-Prüfung vor dem Einfügen
    bool exists = false;
    size_t count = targetRoom.size();
    for (size_t i = 0; i < count; ++i) {
        if (msgPayload.equals(targetRoom.get(i))) {
            exists = true;
            break;
        }
    }

    if (!exists && msgPayload.length() > 0) {
        targetRoom.add(msgPayload);
        addTickerMessage(msgPayload);
    }
}

void ChatManager::handleUserMeshPing(const String& payload) {
    if (payload.length() == 0) return;

    int start = 0;
    while (start < (int)payload.length()) {
        int commaIdx = payload.indexOf(',', start);
        String uidPart;
        if (commaIdx == -1) {
            uidPart = payload.substring(start);
            start = payload.length();
        } else {
            uidPart = payload.substring(start, commaIdx);
            start = commaIdx + 1;
        }
        uidPart.trim();
        if (uidPart.length() == 4) {
            addOrUpdateUser(uidPart.c_str(), false); // false = Remote User
        }
    }
}

void ChatManager::handleMeshTttMessage(const String& payload) {
    String targetUid = getJsonValue(payload, "to");
    if (targetUid.length() != 4) return;

    targetUid.toUpperCase();

    // Überprüfe, ob der Empfänger lokal an diesem Node verbunden ist
    for (auto&& client_item : _ws.getClients()) {
        AsyncWebSocketClient* localClient = getClientPtr(client_item);
        if (localClient && localClient->status() == WS_CONNECTED) {
            auto* s = static_cast<ClientSession*>(localClient->_tempObject);
            if (s && s->uid == targetUid) {
                localClient->text(payload);
                break;
            }
        }
    }
}

#endif
