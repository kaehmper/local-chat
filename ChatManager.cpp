#include "ChatManager.h"
#include <cstring>

// ---------- ClientSession Struktur ----------
struct ClientSession {
    String uid;
    String wsBuffer;
};

// Globaler externer Zeiger auf ChatManager (für statische ESP-NOW Callbacks)
extern ChatManager chatManager;

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
    : _ws("/ws"), _lastActivity(0) {
#if ENABLE_MESH
    _nodeId = 0;
    _lastPingTime = 0;
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
#if ENABLE_MESH
    uint32_t now = millis();
    if (now - _lastPingTime > 60000) { // Alle 60 Sekunden periodischer Sync-Request
        _lastPingTime = now;
        sendMeshBroadcast(2, ""); // SYNC_REQ senden
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

String ChatManager::generateSessionId() {
    // Hardware-RNG des ESP8266 nutzen
    uint16_t r = static_cast<uint16_t>(ESP.random() & 0xFFFF);
    char buf[5];
    std::sprintf(buf, "%04X", r);
    return String(buf);
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
            int end = (endCom != -1 && endCom < endObj) ? endCom : endObj;
            if (end != -1) {
                String val = json.substring(start, end);
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
        sendRoomInit(client);
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
            broadcastMessage(formattedMsg);

#if ENABLE_MESH
            // Übertragungs-Eigenschaften für Echtzeit-Mesh-Nachrichten
            sendMeshBroadcast(1, formattedMsg); // Type 1: CHAT_MSG
#endif
        }
    }
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
        String escaped = buffer.get(i);
        escaped.replace("\\", "\\\\");
        escaped.replace("\"", "\\\"");

        json += "\"" + escaped + "\"";
        if (i < count - 1) {
            json += ",";
        }
    }
    json += "]}";

    client->text(json);
}

// Hilfsfunktionen zur versionsunabhängigen Ermittlung des Zeigers auf einen Client
static inline AsyncWebSocketClient* getClientPtr(AsyncWebSocketClient* p) { return p; }
static inline AsyncWebSocketClient* getClientPtr(AsyncWebSocketClient& r) { return &r; }

void ChatManager::broadcastMessage(const String& msg) {
    String escapedMsg = msg;
    escapedMsg.replace("\\", "\\\\");
    escapedMsg.replace("\"", "\\\"");

    String json;
    json.reserve(64 + msg.length());
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

    String msgPayload = String(packet.message);

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
    Serial.println("[ESP-NOW] Verlaufs-Anforderung von Node " + String(targetNodeId) + " empfangen.");

    // Segmentierte, häppchenweise Übertragung, um den Heap absolut stabil zu halten
    size_t openCount = _openRoom.size();
    for (size_t i = 0; i < openCount; ++i) {
        sendMeshBroadcast(3, _openRoom.get(i)); // Type 3 (SYNC_MSG)
        delay(15); // Kurze non-blocking Atempause für den Network Stack
    }

    // Sync beendet signalisieren
    sendMeshBroadcast(4, ""); // Type 4: SYNC_END
}

void ChatManager::handleSyncResponse(const MeshPacket& packet) {
    String msgPayload = String(packet.message);
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
    }
}

#endif
