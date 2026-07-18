#pragma once

#include <Arduino.h>
#include <functional>
#include "config.h"
#include "SSD1306.h"

/**
 * @class OledManager
 * @brief Verwaltet die Zustandsmaschine des OLED-Displays und des Screensavers.
 *
 * Wechselt zwischen Newsticker und Benutzerliste bei Aktivität,
 * und schaltet bei Inaktivität auf einen sich bewegenden Screensaver um.
 */
class OledManager {
public:
    OledManager();

    /**
     * @brief Initialisiert das OLED-Display.
     * @return true bei Erfolg, false falls das Display nicht gefunden wurde.
     */
    bool begin();

    /**
     * @brief Aktualisiert den Bildschirminhalt (zyklisch aufgerufen).
     * @param now Aktueller Zeitstempel in ms.
     * @param systemActive true, falls das System innerhalb des Aktivitäts-Timeouts verwendet wurde.
     * @param onlineUsersStr Formatierte Liste der Online-Benutzer.
     * @param tickerMsgCount Anzahl der verfügbaren Newsticker-Nachrichten (0-3).
     * @param getTickerMsg Funktion/Lambda zum Abrufen einer Ticker-Nachricht per Index (0 = neueste).
     * @param connectedNodesCount Anzahl der im Mesh erkannten aktiven Remote-Knoten.
     */
    void update(unsigned long now,
                bool systemActive,
                const String& onlineUsersStr,
                size_t tickerMsgCount,
                const std::function<String(size_t)>& getTickerMsg,
                size_t connectedNodesCount);

private:
    SSD1306 _oled;
    unsigned long _lastOledTick;
    bool _screensaverActive;
    int _ssCol;
    int _ssPage;
    bool _showUserList;

    void drawHeader();
};
