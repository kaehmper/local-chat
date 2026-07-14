#include "ChatManager.h"

// ---------- ClientSession Struktur ----------
struct ClientSession {
    String uid;
    bool authenticated;
};

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

String MessageRingBuffer::get(size_t index) const {
    if (index >= _count) return "";
    size_t idx = (_start + index) % Config::MAX_MESSAGES;
    return String(_buffer[idx]);
}


// ==================== ChatManager ====================

ChatManager::ChatManager() 
    : _ws("/ws"), _lastActivity(0) {}

void ChatManager::begin(AsyncWebServer* server) {
    _lastActivity = millis();

    // WebSocket-Event-Callback registrieren
    _ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c, 
                       AwsTemplateType t, void* arg, uint8_t* d, size_t l) {
        this->onWsEvent(s, c, t, arg, d, l);
    });

    server->addHandler(&_ws);
}

void ChatManager::cleanup() {
    _ws.cleanupClients();
}

bool ChatManager::isClientAuthenticated(AsyncWebSocketClient* client) const {
    if (!client) return false;
    auto* session = static_cast<ClientSession*>(client->getTempObject());
    return session && session->authenticated;
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
                            AwsTemplateType type, void* arg, uint8_t* data, size_t len) {
    _lastActivity = millis();

    switch (type) {
        case WS_EVT_CONNECT: {
            String uid = generateSessionId();
            client->setTempObject(new ClientSession{uid, false});
            break;
        }
        case WS_EVT_DISCONNECT: {
            auto* session = static_cast<ClientSession*>(client->getTempObject());
            if (session) {
                delete session;
                client->setTempObject(nullptr);
            }
            break;
        }
        case WS_EVT_DATA: {
            AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
            if (info->opcode == WS_TEXT) {
                String msg = "";
                for (size_t i = 0; i < len; i++) {
                    msg += static_cast<char>(data[i]);
                }
                handleWsTextMessage(client, msg);
            }
            break;
        }
        default:
            break;
    }
}

// Hilfsfunktion zum Parsen einfacher JSON-Schlüssel (Vermeidet Abhängigkeit von ArduinoJson)
static String getJsonValue(const String& json, const String& key) {
    String searchKey = "\"" + key + "\":\"";
    int start = json.indexOf(searchKey);
    if (start != -1) {
        start += searchKey.length();
        int end = json.indexOf("\"", start);
        if (end != -1) {
            return json.substring(start, end);
        }
    } else {
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
    auto* session = static_cast<ClientSession*>(client->getTempObject());
    if (!session) return;

    String type = getJsonValue(message, "type");

    if (type == "init") {
        String room = getJsonValue(message, "room");
        if (room != "locked") room = "open";
        sendRoomInit(client, room);
    } 
    else if (type == "auth") {
        String password = getJsonValue(message, "password");
        bool success = (password == Config::LOCK_PASSWORD);
        session->authenticated = success;

        String resp = "{\"type\":\"auth_result\",\"success\":" + String(success ? "true" : "false") + "}";
        client->text(resp);
    } 
    else if (type == "post") {
        String room = getJsonValue(message, "room");
        if (room != "locked") room = "open";

        if (room == "locked" && !session->authenticated) {
            client->text("{\"type\":\"auth_result\",\"success\":false}");
            return;
        }

        String rawText = getJsonValue(message, "text");
        String cleanText = escapeHtml(rawText);
        if (cleanText.length() > Config::MAX_MSG_LENGTH) {
            cleanText = cleanText.substring(0, Config::MAX_MSG_LENGTH);
        }

        if (cleanText.length() > 0) {
            String formattedMsg = "[" + session->uid + "] " + cleanText;
            if (room == "locked") {
                _lockedRoom.add(formattedMsg);
            } else {
                _openRoom.add(formattedMsg);
            }
            broadcastMessage(room, formattedMsg);
        }
    }
}

void ChatManager::sendRoomInit(AsyncWebSocketClient* client, const String& room) {
    auto* session = static_cast<ClientSession*>(client->getTempObject());
    if (!session) return;

    String json;
    json.reserve(1024);

    json += "{\"type\":\"init\",";
    json += "\"room\":\"" + room + "\",";
    json += "\"uid\":\"" + session->uid + "\",";
    json += "\"authenticated\":" + String(session->authenticated ? "true" : "false") + ",";
    json += "\"messages\":[";

    const MessageRingBuffer& buffer = (room == "locked") ? _lockedRoom : _openRoom;
    size_t count = buffer.size();
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

void ChatManager::broadcastMessage(const String& room, const String& msg) {
    String escapedMsg = msg;
    escapedMsg.replace("\\", "\\\\");
    escapedMsg.replace("\"", "\\\"");

    String json = "{\"type\":\"msg\",\"room\":\"" + room + "\",\"msg\":\"" + escapedMsg + "\"}";

    for (auto* client : _ws.getClients()) {
        if (client && client->status() == WS_CONNECTED) {
            auto* session = static_cast<ClientSession*>(client->getTempObject());
            if (session) {
                if (room == "locked" && !session->authenticated) {
                    continue;
                }
                client->text(json);
            }
        }
    }
}
