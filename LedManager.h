#pragma once

#include <Arduino.h>
#include "config.h"

/**
 * @class LedManager
 * @brief Verwaltet die Aktivitäts-LED mit nicht-blockierenden Blinksequenzen.
 *
 * Bevorzugt hoch-prioritäre Impulse (z.B. bei Nachrichteneingang oder Benutzerverbindung)
 * und fällt danach in den Standard-Aktivitätsstatus zurück.
 */
class LedManager {
public:
    LedManager();

    /**
     * @brief Initialisiert den LED-GPIO-Pin.
     */
    void begin();

    /**
     * @brief Triggert 3 vollständige Blink-Zyklen (An-Aus) bei neuen Nachrichten.
     */
    void triggerMessagePulse();

    /**
     * @brief Triggert einen kurzen Blink-Zyklus bei einem neuen Connect.
     */
    void triggerConnectPulse();

    /**
     * @brief Aktualisiert den nicht-blockierenden LED-Status. Sollte zyklisch aufgerufen werden.
     * @param now Aktuelle Zeit in Millisekunden.
     */
    void update(unsigned long now);

    /**
     * @brief Gibt zurück, ob aktuell eine prioritäre Pulssequenz aktiv ist.
     */
    bool isPulseActive() const { return _pulseTransitionsLeft > 0; }

private:
    int _pulseTransitionsLeft;         // Verbleibende LED-Zustandswechsel
    unsigned long _nextTransitionTime; // Zeitpunkt des nächsten Wechsels in ms
    bool _pulseLedOn;                  // Ob die LED im Puls-Zustand an sein soll
    uint32_t _currentTransitionDuration; // Aktuelle Dauer pro Zustand in ms
};
