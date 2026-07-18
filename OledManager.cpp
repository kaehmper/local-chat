#include "OledManager.h"

OledManager::OledManager()
    : _oled(Config::OLED_I2C_ADDR, Config::OLED_SDA, Config::OLED_SCL),
      _lastOledTick(0),
      _screensaverActive(false),
      _ssCol(0),
      _ssPage(0),
      _showUserList(false) {}

bool OledManager::begin() {
    if (!Config::ENABLE_OLED) return false;
    return _oled.begin();
}

void OledManager::drawHeader() {
    _oled.setCursor(0, 0);
    _oled.print("=== [CardijnChat] ===");
}

void OledManager::update(unsigned long now,
                         bool systemActive,
                         const String& onlineUsersStr,
                         size_t tickerMsgCount,
                         const std::function<String(size_t)>& getTickerMsg,
                         size_t connectedNodesCount) {
    if (!Config::ENABLE_OLED) return;

    if (!systemActive) {
        static unsigned long lastUptimeUpdate = 0;
        bool forceRedraw = false;

        // Triggere sofortigen Neuaufbau beim Wechsel in den Screensaver-Modus
        if (!_screensaverActive) {
            _screensaverActive = true;
            _lastOledTick = now;
            lastUptimeUpdate = now;
            _oled.clear();

            // Bewege die Position des Textes zufällig / versetzt
            _ssCol = (_ssCol + 15) % 45; // Max. Spalte, damit Text noch auf Bildschirm passt
            _ssPage = (_ssPage + 1) % 5; // Max. Page, damit vierzeiliger Text passt (ssPage + 3 <= 7)
            forceRedraw = true;
        }

        // Position alle 5 Sekunden verschieben (um Einbrennen zu verhindern)
        bool positionShifted = false;
        if (now - _lastOledTick >= 5000) {
            _lastOledTick = now;
            _oled.clear();
            _ssCol = (_ssCol + 15) % 45;
            _ssPage = (_ssPage + 1) % 5;
            positionShifted = true;
        }

        // Uptime jede Sekunde aktualisieren
        if (forceRedraw || positionShifted || (now - lastUptimeUpdate >= 1000)) {
            lastUptimeUpdate = now;

            // Berechne die Uptime des Geräts in h m s Format
            unsigned long total_secs = now / 1000;
            unsigned int hours = total_secs / 3600;
            unsigned int minutes = (total_secs % 3600) / 60;
            unsigned int seconds = total_secs % 60;
            String runtimeStr = String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";

            // Wenn neu gezeichnet wird oder die Position wechselt, zeichne alle 4 Zeilen
            if (forceRedraw || positionShifted) {
                _oled.setCursor(_ssCol, _ssPage);
                _oled.print(Config::CHATNAME);
                _oled.setCursor(_ssCol, _ssPage + 1);
                _oled.print("10.10.10.1");
            }

            // Zeichne Uptime-Zeile (mit Leerzeichen gepolstert, um Reste zu überschreiben)
            _oled.setCursor(_ssCol, _ssPage + 2);
            _oled.print((runtimeStr + "   ").c_str());

            // Zeichne Nodes-Zeile (mit Leerzeichen gepolstert, um Reste zu überschreiben)
            String nodesStr = "Nodes: " + String(connectedNodesCount);
            _oled.setCursor(_ssCol, _ssPage + 3);
            _oled.print((nodesStr + "   ").c_str());
        }
        return;
    }

    // Wenn Aktivität erkannt wurde, aber der Screensaver noch aktiv war
    if (_screensaverActive) {
        _screensaverActive = false;
        _oled.clear();
        _lastOledTick = 0; // Sofortiges Neuzeichnen triggern
        _showUserList = false; // Zurücksetzen auf die Nachrichten- bzw. AP-Info-Ansicht
    }

    // Ticker-Rotation / Bildschirm-Wechsel alle 5 Sekunden
    if (now - _lastOledTick >= 5000 || _lastOledTick == 0) {
        _lastOledTick = now;
        _oled.clear();
        drawHeader();

        if (_showUserList) {
            // Online-Nutzerliste anzeigen
            _oled.printWrapped(onlineUsersStr, 2, 6);
        } else {
            if (tickerMsgCount == 0) {
                // Standby/Boot-Bildschirm bei fehlenden Nachrichten
                _oled.setCursor(0, 2);
                _oled.print("AP:  ");
                _oled.print(Config::CHATNAME);

                _oled.setCursor(0, 4);
                _oled.print("IP:  10.10.10.1");

                _oled.setCursor(0, 6);
                _oled.print("Warte auf Chat...");
            } else {
                // Die letzten 3 eindeutigen Nachrichten zusammen anzeigen (älteste oben, neueste unten)
                String msgStr = "";
                for (size_t i = 0; i < tickerMsgCount; ++i) {
                    msgStr += getTickerMsg(tickerMsgCount - 1 - i);
                    if (i < tickerMsgCount - 1) {
                        msgStr += "\n";
                    }
                }
                _oled.printWrapped(msgStr, 1, 7);
            }
        }

        // Zustand umschalten für den nächsten Wechsel
        _showUserList = !_showUserList;
    }
}
