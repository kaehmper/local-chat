#include "LedManager.h"

LedManager::LedManager()
    : _pulseTransitionsLeft(0), _nextTransitionTime(0), _pulseLedOn(false) {}

void LedManager::begin() {
    pinMode(Config::ACTIVITY_LED, OUTPUT);
    digitalWrite(Config::ACTIVITY_LED, Config::ACTIVITY_REVERSE ? HIGH : LOW);
}

void LedManager::triggerMessagePulse() {
    _pulseTransitionsLeft = 6;
    _pulseLedOn = true;
    _nextTransitionTime = millis();
    update(millis());
}

void LedManager::triggerConnectPulse() {
    _pulseTransitionsLeft = 2;
    _pulseLedOn = true;
    _nextTransitionTime = millis();
    update(millis());
}

void LedManager::update(unsigned long now) {
    if (_pulseTransitionsLeft <= 0) return;

    if (now >= _nextTransitionTime) {
        bool level = _pulseLedOn ^ Config::ACTIVITY_REVERSE;
        digitalWrite(Config::ACTIVITY_LED, level ? HIGH : LOW);

        _nextTransitionTime = now + 120; // 120ms pro Zustand
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
