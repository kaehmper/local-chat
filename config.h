#pragma once

#include <Arduino.h>

/**
 * @file Config.h
 * @brief Zentrale Konfiguration für den PopupChat.
 */

namespace Config {

// ---------- WLAN & Netzwerk ----------
constexpr char CHATNAME[]        = "PopupChat";             ///< Name des Access Points (SSID)
constexpr char BLURB[]           = "Lokaler Chat in zwei Räumen"; ///< Slogan/Untertitel
constexpr char LOCK_PASSWORD[]   = "secret";                ///< Passwort für den geschützten Raum
constexpr char AUTH_SALT[]       = "popup2024";             ///< Salt für kryptographische Verifizierungen
constexpr uint16_t HTTP_PORT     = 80;                      ///< Port des Webservers
constexpr uint16_t DNS_PORT      = 53;                      ///< Port des DNS-Servers

// ---------- Chat-Einstellungen ----------
constexpr size_t MAX_MESSAGES    = 40;                      ///< Maximale Anzahl an Nachrichten pro Raum
constexpr size_t MAX_MSG_LENGTH  = 200;                     ///< Maximale Länge einer Nachricht in Zeichen

// ---------- Aktivitäts-LED & Timer ----------
constexpr uint8_t ACTIVITY_LED   = 2;                       ///< GPIO der Aktivitäts-LED (ESP8266 Builtin LED ist meist GPIO 2)
constexpr bool ACTIVITY_REVERSE  = true;                    ///< true, wenn die LED Low-Active ist (beim ESP8266 meist der Fall)
constexpr uint32_t ACTIVITY_DURATION = 60000;               ///< Inaktivitäts-Timeout in ms (60 Sekunden)
constexpr uint32_t TICK_INTERVAL     = 1000;                ///< Takt-Intervall für die LED-Steuerung in ms

} // namespace Config
