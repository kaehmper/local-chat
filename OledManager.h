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
enum ScreenView {
    VIEW_MESSAGES = 0,
    VIEW_USERS = 1,
    VIEW_SYSTEM = 2
};

#define NUM_STARS 25

struct Star {
    float x;
    uint8_t y;
    float speed;
    uint8_t pattern;
};

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
     * @param onlineUsersCount Anzahl der Online-Benutzer.
     * @param getUserUid Funktion/Lambda zum Abrufen einer UID per Index.
     * @param isUserLocal Funktion/Lambda zur Prüfung, ob ein Benutzer lokal verbunden ist.
     * @param roomMsgCount Anzahl der verfügbaren Chat-Nachrichten.
     * @param getRoomMsg Funktion/Lambda zum Abrufen einer Chat-Nachricht per Index.
     * @param connectedNodesCount Anzahl der im Mesh erkannten aktiven Remote-Knoten.
     * @param strongestRssi RSSI-Wert des stärksten remote Knotens.
     * @param getNodeId Funktion/Lambda zum Abrufen einer Node ID per Index.
     * @param getNodeRssi Funktion/Lambda zum Abrufen eines Node RSSI per Index.
     */
    void update(unsigned long now,
                bool systemActive,
                size_t onlineUsersCount,
                const std::function<String(size_t)>& getUserUid,
                const std::function<bool(size_t)>& isUserLocal,
                size_t roomMsgCount,
                const std::function<String(size_t)>& getRoomMsg,
                size_t connectedNodesCount,
                int strongestRssi,
                const std::function<String(size_t)>& getNodeId,
                const std::function<int(size_t)>& getNodeRssi);

private:
    SSD1306 _oled;
    unsigned long _lastOledTick;
    bool _screensaverActive;
    int _ssCol;
    int _ssPage;

    // Starfield-Screensaver
    Star _stars[NUM_STARS];
    bool _starsInitialized;
    unsigned long _lastStarUpdate;

    // Button-Entprellung und manueller Modus
    unsigned long _buttonPressStart;
    bool _buttonLastState;
    bool _manualMode;
    unsigned long _lastButtonPressTime;
    ScreenView _currentView;

    void drawHeader(const char* title, const uint8_t* iconData);
    void drawMessagesScreen(size_t msgCount, const std::function<String(size_t)>& getMsg);
    void drawUsersScreen(size_t userCount, const std::function<String(size_t)>& getUserUid, const std::function<bool(size_t)>& isUserLocal);
    void drawSystemScreen(unsigned long now,
                         size_t connectedNodesCount,
                         int strongestRssi,
                         const std::function<String(size_t)>& getNodeId,
                         const std::function<int(size_t)>& getNodeRssi);
};
