#pragma once

#include <Arduino.h>

/**
 * @file Config.h
 * @brief Zentrale Konfiguration für den PopupChat.
 */

#define ENABLE_MESH 1 // Auf 0 setzen, um Mesh zu deaktivieren und als Standalone-Knoten zu laufen

namespace Config {

// ---------- WLAN & Netzwerk ----------
constexpr char CHATNAME[]        = "CardijnChat";           ///< Name des Access Points (SSID)
constexpr char BLURB[]           = "Lokaler CardijnChat";   ///< Slogan/Untertitel
constexpr uint16_t HTTP_PORT     = 80;                      ///< Port des Webservers
constexpr uint16_t DNS_PORT      = 53;                      ///< Port des DNS-Servers
constexpr float MAX_WIFI_POWER   = 20.5f;                   ///< Maximale WLAN-Sendeleistung in dBm (0 bis 20.5)

// ---------- Mesh-Netzwerk ----------
constexpr uint8_t MESH_CHANNEL   = 6;                       ///< Wi-Fi Kanal (AP und Mesh MÜSSEN auf dem gleichen Kanal liegen!)

// ---------- Chat-Einstellungen ----------
constexpr size_t MAX_MESSAGES    = 40;                      ///< Maximale Anzahl an Nachrichten pro Raum
constexpr size_t MAX_MSG_LENGTH  = 200;                     ///< Maximale Länge einer Nachricht in Zeichen

// ---------- Aktivitäts-LED & Timer ----------
constexpr uint8_t ACTIVITY_LED   = 3;                       ///< GPIO der Aktivitäts-LED (für GPIO 3)
constexpr bool ACTIVITY_REVERSE  = true;                    ///< true, wenn die LED Low-Active ist (beim ESP8266 meist der Fall)
constexpr uint32_t ACTIVITY_DURATION = 20000;               ///< Inaktivitäts-Timeout in ms (20 Sekunden)
constexpr uint32_t TICK_INTERVAL     = 1000;                ///< Takt-Intervall für die LED-Steuerung in ms

// LED Puls-Konfigurationen
constexpr uint32_t MSG_PULSE_DURATION_MS = 20;              ///< Dauer für den AN-Zustand bei Nachrichteneingang in ms
constexpr uint32_t MSG_PULSE_COUNT       = 4;               ///< Anzahl der vollständigen Blinkzyklen bei Nachrichteneingang
constexpr uint32_t CONNECT_PULSE_DURATION_MS = 40;          ///< Dauer für den AN-Zustand bei Connect in ms
constexpr uint32_t CONNECT_PULSE_COUNT       = 2;           ///< Anzahl der vollständigen Blinkzyklen bei Connect
constexpr uint32_t PULSE_PAUSE_DURATION_MS   = 50;          ///< Pause zwischen Blinks (AUS-Zustand) in ms

// ---------- OLED-Display (SSD1306) ----------
constexpr bool ENABLE_OLED       = true;                    ///< Setzen Sie dies auf false, um das Display zu deaktivieren
constexpr uint8_t OLED_SDA       = 4;                       ///< GPIO für SDA (standardmäßig GPIO 4 / D2)
constexpr uint8_t OLED_SCL       = 5;                       ///< GPIO für SCL (standardmäßig GPIO 5 / D1)
constexpr uint8_t OLED_I2C_ADDR  = 0x3C;                    ///< 7-Bit I2C-Adresse (entspricht 8-Bit-Schreibadresse 0x78)

// ---------- Hardware Taster ----------
constexpr uint8_t BUTTON_PIN     = 1;                       ///< GPIO des Hardware Tasters zum Umschalten (TX Pin, active-low)

} // namespace Config
