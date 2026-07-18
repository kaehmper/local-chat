#include "ChatManager.h"
#include <cstring>

// ---------- ClientSession Struktur ----------
struct ClientSession {
    String uid;
    String wsBuffer;
};

// Hilfsfunktionen zur versionsunabhängigen Ermittlung des Zeigers auf einen Client (muss ganz oben deklariert sein)
static inline AsyncWebSocketClient* getClientPtr(AsyncWebSocketClient* p) { return p; }
static inline AsyncWebSocketClient* getClientPtr(AsyncWebSocketClient& r) { return &r; }

ChatManager::ChatManager()
    : _ws("/ws"),
      _lastActivity(0),
      _tickerCount(0),
      _onlineUsersCount(0),
      _lastUserListBroadcastTime(0) {
    std::memset(_onlineUsers, 0, sizeof(_onlineUsers));
}

void ChatManager::begin(AsyncWebServer* server) {
    _lastActivity = millis();

    // LED initialisieren
    _ledManager.begin();

    // OLED initialisieren
    _oledManager.begin();

    // WebSocket-Event-Callback registrieren
    _ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                       AwsEventType t, void* arg, uint8_t* d, size_t l) {
        this->onWsEvent(s, c, t, arg, d, l);
    });

    server->addHandler(&_ws);

    // Mesh-Manager initialisieren mit Lambda-Callbacks zur Entkopplung
    _meshManager.begin(
        [this](const String& msg) { this->handleMeshIncomingMsg(msg); },
        [this]() { this->handleMeshSyncEnd(); },
        [this](const String& payload) { this->handleMeshUserPing(payload); },
        [this](const String& payload) { this->handleMeshTttMsg(payload); }
    );
}

void ChatManager::update() {
    _ws.cleanupClients();

    uint32_t now = millis();

    // Periodischer User-Listen Broadcast (alle 4 Sekunden)
    if (now - _lastUserListBroadcastTime > 4000) {
        _lastUserListBroadcastTime = now;
        broadcastUserList();
    }

    // Mesh-Updates ausführen (Historien-Synchronisierung etc.)
    _meshManager.update(
        [this](size_t idx) { return String(_openRoom.get(idx)); },
        _openRoom.size()
    );

    // OLED-Zustandsmaschine aktualisieren
    bool systemActive = (now - _lastActivity) < Config::ACTIVITY_DURATION;
    updateOnlineUsersList();
    _oledManager.update(
        now,
        systemActive,
        _onlineUsersCount,
        [this](size_t idx) { return String(_onlineUsers[idx].uid); },
        [this](size_t idx) { return _onlineUsers[idx].isLocal; },
        _openRoom.size(),
        [this](size_t idx) { return String(_openRoom.get(idx)); },
        getConnectedNodesCount()
    );

    // Nicht-blockierendes LED-Blinken aktualisieren
    _ledManager.update(now);
}

void ChatManager::handleMeshIncomingMsg(const String& msg) {
    // Wenn über das Mesh eine Chat-Nachricht ankommt, prüfen wir sie auf Dubletten
    bool exists = false;
    size_t count = _openRoom.size();
    for (size_t i = 0; i < count; ++i) {
        if (msg.equals(_openRoom.get(i))) {
            exists = true;
            break;
        }
    }

    if (!exists && msg.length() > 0) {
        _openRoom.add(msg);
        addTickerMessage(msg);
        broadcastMessage(msg);

        // LED pulsieren lassen bei neuer Nachricht
        _ledManager.triggerMessagePulse();

        // Systemaktivität aktualisieren (Nachrichteneingang gilt als Benutzeraktion)
        _lastActivity = millis();
    }
}

void ChatManager::handleMeshSyncEnd() {
    Serial.println("[ESP-NOW] Historien-Synchronisierung abgeschlossen. Aktualisiere Clients...");
    // Sende Rauminitialisierungsdaten an alle verbundenen WebSockets, damit diese den Verlauf sehen
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

void ChatManager::handleMeshUserPing(const String& payload) {
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
        if (TicTacToeManager::isValidUid(uidPart)) {
            addOrUpdateUser(uidPart.c_str(), false); // false = Remote User im Mesh
        }
    }
}

void ChatManager::handleMeshTttMsg(const String& payload) {
    // Da Tic-Tac-Toe Züge als Benutzeraktivität zählen, wecken wir den Screensaver auf
    _lastActivity = millis();

    String targetUid = "";
    // Zum Parsen von targetUid
    String secureJson = "";
    // Reiner Routing-Pass: Sende Payload an den passenden lokalen WebSocket-Client
    // Da die Payload bereits sicher durch buildSecureMessage vom Absender-Knoten rekonstruiert wurde,
    // können wir sie direkt an den Ziel-Client routen
    int toIdx = payload.indexOf("\"to\":\"");
    if (toIdx != -1) {
        toIdx += 6;
        targetUid = payload.substring(toIdx, toIdx + 4);
        targetUid.toUpperCase();
    }

    if (TicTacToeManager::isValidUid(targetUid)) {
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
                default:   result += '\\'; break;
            }
        } else {
            result += val[i];
        }
    }
    return result;
}

static String getJsonValue(const String& json, const String& key) {
    String searchKey = "\"" + key + "\":\"";
    int start = json.indexOf(searchKey);
    if (start != -1) {
        start += searchKey.length();
        int end = -1;
        int curr = start;
        while (curr < (int)json.length()) {
            int nextQuote = json.indexOf('"', curr);
            if (nextQuote == -1) break;
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
                String val = json.substring(start);
                val.replace("\"", "");
                val.trim();
                return val;
            }
        }
    }
    return "";
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

    // 2. Nur remote Online-Nutzer prüfen
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (!_onlineUsers[i].isLocal && uid.equalsIgnoreCase(_onlineUsers[i].uid)) {
            return true;
        }
    }

    return false;
}

String ChatManager::generateSessionId() {
    char buf[5];
    String uid;
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
                // DoS / Heap Exhaustion Prevention: Block frames > 2048 bytes
                if (info->len > 2048) {
                    client->close();
                    break;
                }

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

void ChatManager::handleWsTextMessage(AsyncWebSocketClient* client, const String& message) {
    auto* session = static_cast<ClientSession*>(client->_tempObject);
    if (!session) return;

    String type = getJsonValue(message, "type");

    if (type == "init") {
        String requestedUid = getJsonValue(message, "uid");
        bool isValid = TicTacToeManager::isValidUid(requestedUid);

        if (isValid) {
            // Reconnection: Wenn bereits ein anderer Client diese UID hat, trennen wir ihn
            for (auto&& client_item : _ws.getClients()) {
                AsyncWebSocketClient* otherClient = getClientPtr(client_item);
                if (otherClient && otherClient != client && otherClient->status() == WS_CONNECTED) {
                    auto* otherSession = static_cast<ClientSession*>(otherClient->_tempObject);
                    if (otherSession && otherSession->uid.equalsIgnoreCase(requestedUid)) {
                        Serial.println("[Session] Schließe alten/doppelten Client für UID: " + requestedUid);
                        otherSession->uid = "";
                        otherClient->close();
                    }
                }
            }
        }

        if (isValid && !isUidInUse(requestedUid)) {
            session->uid = requestedUid;
            session->uid.toUpperCase();
            Serial.println("[Session] Vorhandene UID übernommen: " + session->uid);
        } else {
            if (isValid && isUidInUse(requestedUid)) {
                Serial.println("[Session] Angeforderte UID bereits vergeben: " + requestedUid + ", generiere neue.");
            }
            session->uid = generateSessionId();
        }

        // Als lokalen Benutzer registrieren und Online-Liste verteilen
        addOrUpdateUser(session->uid.c_str(), true);
        broadcastUserList();

        // Kurzer Connect-LED-Puls
        _ledManager.triggerConnectPulse();

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

            // LED-Nachrichten-Puls triggern
            _ledManager.triggerMessagePulse();

            // Aktivität registrieren
            _lastActivity = millis();

            // Echtzeit-Mesh-Übertragung
            _meshManager.sendBroadcast(1, formattedMsg); // Type 1: CHAT_MSG
        }
    }
}

void ChatManager::sendRoomInit(AsyncWebSocketClient* client) {
    auto* session = static_cast<ClientSession*>(client->_tempObject);
    if (!session) return;

    size_t count = _openRoom.size();
    size_t requiredSize = 128 + session->uid.length();
    for (size_t i = 0; i < count; ++i) {
        requiredSize += std::strlen(_openRoom.get(i)) + 8;
    }

    String json;
    json.reserve(requiredSize);

    json += "{\"type\":\"init\",";
    json += "\"uid\":\"" + session->uid + "\",";
    json += "\"messages\":[";

    for (size_t i = 0; i < count; ++i) {
        json += "\"" + escapeJsonStringValue(_openRoom.get(i)) + "\"";
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

    for (size_t i = 0; i < _tickerCount; ++i) {
        if (_tickerMessages[i].equals(msg)) {
            for (size_t j = i; j > 0; --j) {
                _tickerMessages[j] = _tickerMessages[j - 1];
            }
            _tickerMessages[0] = msg;
            return;
        }
    }

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

    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (std::strcmp(_onlineUsers[i].uid, uid) == 0) {
            _onlineUsers[i].lastSeen = now;
            _onlineUsers[i].isLocal = isLocal;
            return;
        }
    }

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

    // 1. Behalte valide Remote-Benutzer, die jünger als 10 Sekunden sind
    size_t writeIdx = 0;
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (!_onlineUsers[i].isLocal) {
            if (now - _onlineUsers[i].lastSeen < 10000) {
                _onlineUsers[writeIdx++] = _onlineUsers[i];
            }
        }
    }
    _onlineUsersCount = writeIdx;

    // 2. Füge alle aktuell lokal verbundenen WebSocket-Nutzer wieder hinzu
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

    // 1. Lokale Benutzer sammeln, um sie via ESP-NOW zu senden
    String localUids = "";
    for (size_t i = 0; i < _onlineUsersCount; ++i) {
        if (_onlineUsers[i].isLocal) {
            if (localUids.length() > 0) localUids += ",";
            localUids += _onlineUsers[i].uid;
        }
    }

    // Sende lokalen User-Ping an andere Mesh-Knoten (auch wenn localUids leer ist, zur Entdeckung)
    _meshManager.sendBroadcast(5, localUids);

    // 2. Erstelle JSON mit allen aktiven Online-Nutzern und sende an alle WebSockets
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

    // Wecken des Screensavers bei Tic-Tac-Toe Interaktion
    _lastActivity = millis();

    String targetUid = "";
    String secureMsg = "";

    // Sichere Rekonstruktion mit TicTacToeManager, um Identitäts-Spoofing zu verhindern
    if (!TicTacToeManager::buildSecureMessage(session->uid, message, targetUid, secureMsg)) {
        return;
    }

    // 1. Prüfen, ob der Ziel-User lokal an diesem Node verbunden ist
    bool foundLocally = false;
    for (auto&& client_item : _ws.getClients()) {
        AsyncWebSocketClient* localClient = getClientPtr(client_item);
        if (localClient && localClient->status() == WS_CONNECTED) {
            auto* s = static_cast<ClientSession*>(localClient->_tempObject);
            if (s && s->uid == targetUid) {
                localClient->text(secureMsg);
                foundLocally = true;
                break;
            }
        }
    }

    // 2. Wenn nicht lokal, via ESP-NOW Mesh broadcasten
    if (!foundLocally) {
        _meshManager.sendBroadcast(6, secureMsg); // Type 6: TTT_MSG
    }
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
