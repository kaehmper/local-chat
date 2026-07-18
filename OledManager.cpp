#include "OledManager.h"
#include <cstdio>

// 8x8 Icon Bitmaps (each byte is a vertical column from left to right, LSB is the top pixel)
// Message bubble icon
static const uint8_t icon_msg[8] = { 0x00, 0x3E, 0x41, 0x45, 0x45, 0x3E, 0x08, 0x00 };

// Person/Users silhouette icon
static const uint8_t icon_users[8] = { 0x00, 0x1C, 0x22, 0x7E, 0x7E, 0x22, 0x1C, 0x00 };

// Info (i) icon
static const uint8_t icon_info[8] = { 0x00, 0x3E, 0x41, 0x49, 0x49, 0x41, 0x3E, 0x00 };

OledManager::OledManager()
    : _oled(Config::OLED_I2C_ADDR, Config::OLED_SDA, Config::OLED_SCL),
      _lastOledTick(0),
      _screensaverActive(false),
      _ssCol(0),
      _ssPage(0),
      _buttonPressStart(0),
      _buttonLastState(HIGH),
      _manualMode(false),
      _lastButtonPressTime(0),
      _currentView(VIEW_MESSAGES) {}

bool OledManager::begin() {
    if (!Config::ENABLE_OLED) return false;
    pinMode(Config::BUTTON_PIN, INPUT_PULLUP);
    return _oled.begin();
}

void OledManager::drawHeader(const char* title, const uint8_t* iconData) {
    // Page 0: Draw icon, then title
    _oled.setCursor(0, 0);
    if (iconData) {
        _oled.drawPattern(iconData);
    }
    _oled.print(" ");
    _oled.print(title);

    // Page 1: Separator line (solid bottom border)
    _oled.setCursor(0, 1);
    uint8_t linePattern[8] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
    for (int i = 0; i < 16; ++i) {
        _oled.drawPattern(linePattern);
    }
}

void OledManager::drawMessagesScreen(size_t msgCount, const std::function<String(size_t)>& getMsg) {
    char headerTitle[22];
    std::sprintf(headerTitle, "MESSAGES (%d)", (int)msgCount);
    drawHeader(headerTitle, icon_msg);

    if (msgCount == 0) {
        _oled.setCursor(0, 4);
        _oled.print("  No messages yet.  ");
        // Clear remaining lines
        for (uint8_t p = 2; p < 8; ++p) {
            if (p != 4) {
                _oled.setCursor(0, p);
                _oled.print("                     ");
            }
        }
        return;
    }

    // Find how many of the most recent messages can fit within Pages 2 to 7 (6 lines)
    size_t scanCount = msgCount > 6 ? 6 : msgCount;

    struct MsgInfo {
        size_t index;
        String text;
        int lines;
    };
    MsgInfo candidates[6];
    size_t candidateCount = 0;

    int totalLines = 0;
    for (size_t i = 0; i < scanCount; ++i) {
        size_t idx = msgCount - 1 - i;
        String text = getMsg(idx);

        // Count wrapped lines
        int lines = 0;
        int colCount = 0;
        for (size_t j = 0; j < text.length(); ++j) {
            char c = text[j];
            if (c == '\r') continue;
            if (c == '\n') {
                lines++;
                colCount = 0;
                continue;
            }
            if (colCount >= 21) {
                lines++;
                colCount = 0;
            }
            colCount++;
        }
        lines++; // Count the last/current line

        if (totalLines + lines > 6) {
            // If even the newest single message exceeds 6 lines, display it up to 6 lines
            if (candidateCount == 0) {
                candidates[candidateCount++] = {idx, text, 6};
                totalLines = 6;
            }
            break;
        }

        candidates[candidateCount++] = {idx, text, lines};
        totalLines += lines;
    }

    // Draw candidates chronologically (oldest at the top, newest at the bottom of candidate list)
    uint8_t currentPage = 2;
    for (int i = (int)candidateCount - 1; i >= 0; --i) {
        _oled.printWrapped(candidates[i].text, currentPage, candidates[i].lines);
        currentPage += candidates[i].lines;
    }

    // Clear any remaining lines below the messages
    for (uint8_t p = currentPage; p < 8; ++p) {
        _oled.setCursor(0, p);
        _oled.print("                     ");
    }
}

void OledManager::drawUsersScreen(size_t userCount, const std::function<String(size_t)>& getUserUid, const std::function<bool(size_t)>& isUserLocal) {
    char headerTitle[22];
    std::sprintf(headerTitle, "USERS (%d)", (int)userCount);
    drawHeader(headerTitle, icon_users);

    if (userCount == 0) {
        _oled.setCursor(0, 4);
        _oled.print("  No online users.  ");
        // Clear remaining lines
        for (uint8_t p = 2; p < 8; ++p) {
            if (p != 4) {
                _oled.setCursor(0, p);
                _oled.print("                     ");
            }
        }
        return;
    }

    // Display up to 6 users (Pages 2 to 7)
    size_t displayCount = userCount;
    bool showMore = false;
    if (userCount > 6) {
        displayCount = 5;
        showMore = true;
    }

    for (size_t i = 0; i < displayCount; ++i) {
        String uid = getUserUid(i);
        bool isLocal = isUserLocal(i);
        String line = isLocal ? "* " : "o ";
        line += uid;
        line += isLocal ? " (Local)" : " (Mesh)";

        // Pad to 21 chars
        while (line.length() < 21) {
            line += " ";
        }

        _oled.setCursor(0, 2 + i);
        _oled.print(line.c_str());
    }

    if (showMore) {
        char moreBuf[22];
        std::sprintf(moreBuf, "... and %d more", (int)(userCount - 5));
        String line = moreBuf;
        while (line.length() < 21) {
            line += " ";
        }
        _oled.setCursor(0, 7);
        _oled.print(line.c_str());
    } else {
        // Clear remaining lines
        for (size_t i = displayCount; i < 6; ++i) {
            _oled.setCursor(0, 2 + i);
            _oled.print("                     ");
        }
    }
}

void OledManager::drawSystemScreen(unsigned long now, size_t connectedNodesCount) {
    drawHeader("SYSTEM INFO", icon_info);

    // Page 2: AP SSID
    String line2 = "AP:   " + String(Config::CHATNAME);
    while (line2.length() < 21) line2 += " ";
    _oled.setCursor(0, 2);
    _oled.print(line2.c_str());

    // Page 3: IP Address
    String line3 = "IP:   10.10.10.1";
    while (line3.length() < 21) line3 += " ";
    _oled.setCursor(0, 3);
    _oled.print(line3.c_str());

    // Page 4: Mesh Name
    String line4 = "Mesh: " + String(Config::MESH_PREFIX);
    if (line4.length() > 21) line4 = line4.substring(0, 21);
    while (line4.length() < 21) line4 += " ";
    _oled.setCursor(0, 4);
    _oled.print(line4.c_str());

    // Page 5: Mesh Channel
    String line5 = "Chan: " + String(Config::MESH_CHANNEL);
    while (line5.length() < 21) line5 += " ";
    _oled.setCursor(0, 5);
    _oled.print(line5.c_str());

    // Page 6: Remote Nodes count
    String line6 = "Nodes: " + String(connectedNodesCount) + " active";
    while (line6.length() < 21) line6 += " ";
    _oled.setCursor(0, 6);
    _oled.print(line6.c_str());

    // Page 7: Uptime
    unsigned long total_secs = now / 1000;
    unsigned int hours = total_secs / 3600;
    unsigned int minutes = (total_secs % 3600) / 60;
    unsigned int seconds = total_secs % 60;
    String runtimeStr = "UP:   " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
    while (runtimeStr.length() < 21) runtimeStr += " ";
    _oled.setCursor(0, 7);
    _oled.print(runtimeStr.c_str());
}

void OledManager::update(unsigned long now,
                         bool systemActive,
                         size_t onlineUsersCount,
                         const std::function<String(size_t)>& getUserUid,
                         const std::function<bool(size_t)>& isUserLocal,
                         size_t roomMsgCount,
                         const std::function<String(size_t)>& getRoomMsg,
                         size_t connectedNodesCount) {
    if (!Config::ENABLE_OLED) return;

    // GPIO 1 button reading and debouncing
    bool currentButtonState = digitalRead(Config::BUTTON_PIN);
    bool buttonPressed = false;

    if (currentButtonState == LOW && _buttonLastState == HIGH) {
        if (now - _lastButtonPressTime > 250) { // 250ms debounce
            buttonPressed = true;
            _lastButtonPressTime = now;
        }
    }
    _buttonLastState = currentButtonState;

    // Handle screensaver transitions & waking up
    if (!systemActive && !_manualMode) {
        static unsigned long lastUptimeUpdate = 0;
        bool forceRedraw = false;

        // Trigger immediate screensaver when entering screensaver mode
        if (!_screensaverActive) {
            _screensaverActive = true;
            _lastOledTick = now;
            lastUptimeUpdate = now;
            _oled.clear();

            _ssCol = (_ssCol + 15) % 45;
            _ssPage = (_ssPage + 1) % 5;
            forceRedraw = true;
        }

        // Handle button wakeup (wake up only!)
        if (buttonPressed) {
            _screensaverActive = false;
            _manualMode = true;
            _oled.clear();
            _lastOledTick = now;
            // Draw current active view immediately
            if (_currentView == VIEW_MESSAGES) {
                drawMessagesScreen(roomMsgCount, getRoomMsg);
            } else if (_currentView == VIEW_USERS) {
                drawUsersScreen(onlineUsersCount, getUserUid, isUserLocal);
            } else {
                drawSystemScreen(now, connectedNodesCount);
            }
            return;
        }

        // Position shift every 5 seconds (to prevent burn-in)
        bool positionShifted = false;
        if (now - _lastOledTick >= 5000) {
            _lastOledTick = now;
            _oled.clear();
            _ssCol = (_ssCol + 15) % 45;
            _ssPage = (_ssPage + 1) % 5;
            positionShifted = true;
        }

        // Update uptime every second
        if (forceRedraw || positionShifted || (now - lastUptimeUpdate >= 1000)) {
            lastUptimeUpdate = now;

            unsigned long total_secs = now / 1000;
            unsigned int hours = total_secs / 3600;
            unsigned int minutes = (total_secs % 3600) / 60;
            unsigned int seconds = total_secs % 60;
            String runtimeStr = String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";

            if (forceRedraw || positionShifted) {
                _oled.setCursor(_ssCol, _ssPage);
                _oled.print(Config::CHATNAME);
                _oled.setCursor(_ssCol, _ssPage + 1);
                _oled.print("10.10.10.1");
            }

            _oled.setCursor(_ssCol, _ssPage + 2);
            _oled.print((runtimeStr + "   ").c_str());

            String nodesStr = "Nodes: " + String(connectedNodesCount);
            _oled.setCursor(_ssCol, _ssPage + 3);
            _oled.print((nodesStr + "   ").c_str());
        }
        return;
    }

    // When activity is recognized but screensaver was still active
    if (_screensaverActive) {
        _screensaverActive = false;
        _oled.clear();
        _lastOledTick = 0; // Trigger immediate redraw
    }

    // Handle button state while awake
    bool forceRedraw = false;
    if (buttonPressed) {
        _manualMode = true;
        _currentView = static_cast<ScreenView>((_currentView + 1) % 3);
        forceRedraw = true;
        _oled.clear();
    }

    // Manage manual mode 60 seconds timeout
    if (_manualMode) {
        if (now - _lastButtonPressTime >= 60000) {
            _manualMode = false;
            _oled.clear();
            _lastOledTick = 0; // Trigger immediate auto-rotation draw
        } else {
            // Draw manual view every 1000 ms or on button press
            if (forceRedraw || (now - _lastOledTick >= 1000) || _lastOledTick == 0) {
                _lastOledTick = now;
                if (_currentView == VIEW_MESSAGES) {
                    drawMessagesScreen(roomMsgCount, getRoomMsg);
                } else if (_currentView == VIEW_USERS) {
                    drawUsersScreen(onlineUsersCount, getUserUid, isUserLocal);
                } else {
                    drawSystemScreen(now, connectedNodesCount);
                }
            }
            return;
        }
    }

    // Auto-rotation every 5 seconds (rotating between Messages, Users, and System info)
    if (now - _lastOledTick >= 5000 || _lastOledTick == 0) {
        _lastOledTick = now;
        _oled.clear();
        _currentView = static_cast<ScreenView>((_currentView + 1) % 3);

        if (_currentView == VIEW_MESSAGES) {
            drawMessagesScreen(roomMsgCount, getRoomMsg);
        } else if (_currentView == VIEW_USERS) {
            drawUsersScreen(onlineUsersCount, getUserUid, isUserLocal);
        } else {
            drawSystemScreen(now, connectedNodesCount);
        }
    }
}
