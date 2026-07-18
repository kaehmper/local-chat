#include "LedManager.h"

LedManager::LedManager()
    : _pulseTransitionsLeft(0), _nextTransitionTime(0), _pulseLedOn(false), _currentTransitionDuration(0) {}

void LedManager::begin() {
    pinMode(Config::ACTIVITY_LED, OUTPUT);
    digitalWrite(Config::ACTIVITY_LED, Config::ACTIVITY_REVERSE ? HIGH : LOW);
}

void LedManager::triggerMessagePulse() {
    _pulseTransitionsLeft = Config::MSG_PULSE_COUNT * 2;
    _pulseLedOn = true;
    _currentTransitionDuration = Config::MSG_PULSE_DURATION_MS;
    _nextTransitionTime = millis();
    update(millis());
}

void LedManager::triggerConnectPulse() {
    _pulseTransitionsLeft = Config::CONNECT_PULSE_COUNT * 2;
    _pulseLedOn = true;
    _currentTransitionDuration = Config::CONNECT_PULSE_DURATION_MS;
    _nextTransitionTime = millis();
    update(millis());
}

void LedManager::update(unsigned long now) {
    if (_pulseTransitionsLeft <= 0) return;

    if (now >= _nextTransitionTime) {
        bool level = _pulseLedOn ^ Config::ACTIVITY_REVERSE;
        digitalWrite(Config::ACTIVITY_LED, level ? HIGH : LOW);

        // Bestimme die Dauer des aktuellen Zustands:
        // Wenn die LED an ist (aktiviert wurde), bleibt sie für die Puls-Dauer an.
        // Wenn die LED aus ist (deaktiviert wurde), bleibt sie für die Pause-Dauer aus.
        uint32_t duration = _pulseLedOn ? _currentTransitionDuration : Config::PULSE_PAUSE_DURATION_MS;

        _nextTransitionTime = now + duration;
        _pulseLedOn = !_pulseLedOn;
        _pulseTransitionsLeft--;

        if (_pulseTransitionsLeft <= 0) {
            // LED wieder in den passiven Grundzustand (AUS) versetzen
            digitalWrite(Config::ACTIVITY_LED, Config::ACTIVITY_REVERSE ? HIGH : LOW);
        }
    }
}

void LedManager::setStandardState(bool state) {
    if (isPulseActive()) return;
    bool ledLevel = state ^ Config::ACTIVITY_REVERSE;
    digitalWrite(Config::ACTIVITY_LED, ledLevel ? HIGH : LOW);
}
