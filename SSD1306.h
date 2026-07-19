#pragma once

#include <Arduino.h>
#include <Wire.h>

/**
 * @class SSD1306
 * @brief Eine extrem speicher- und laufzeitoptimierte Implementierung für 128x64 OLED Displays.
 *
 * Nutzt direkten Page-Addressing Modus, um ohne RAM-Framebuffer auszukommen (0 Bytes Heap-Verbrauch!).
 */
class SSD1306 {
public:
    SSD1306(uint8_t address = 0x3C, uint8_t sda = 4, uint8_t scl = 5);
    ~SSD1306() = default;

    /**
     * @brief Initialisiert das OLED-Display über I2C.
     */
    bool begin();

    /**
     * @brief Löscht den gesamten Bildschirminhalt.
     */
    void clear();

    /**
     * @brief Schaltet das Display ein oder aus (für Screensaver/Energiesparen).
     */
    void setPowerSave(bool enable);

    /**
     * @brief Setzt den Cursor auf eine bestimmte Spalte und Zeile (Page: 0-7).
     * @param col Spalte (0-127)
     * @param page Zeile/Page (0-7, jede Page ist 8 Pixel hoch)
     */
    void setCursor(uint8_t col, uint8_t page);

    /**
     * @brief Schreibt ein einzelnes ASCII-Zeichen auf das Display.
     * @param c Das Zeichen.
     * @return 1 bei Erfolg.
     */
    size_t write(char c);

    /**
     * @brief Schreibt einen nullterminierten String auf das Display.
     * @param str Der String.
     */
    void print(const char* str);

    /**
     * @brief Schreibt einen String mit automatischem Zeilenumbruch.
     * @param str Der String.
     * @param startPage Die Zeile, ab der gezeichnet werden soll (0-7).
     * @param maxPages Die maximale Anzahl der zu beschreibenden Zeilen.
     */
    void printWrapped(const String& str, uint8_t startPage, uint8_t maxPages);

    /**
     * @brief Zeichnet ein einzelnes benutzerdefiniertes 8x8 Pixel-Muster an der aktuellen Position.
     * @param data Array mit 8 Bytes, die die vertikalen Spalten des Musters definieren.
     */
    void drawPattern(const uint8_t* data);

    /**
     * @brief Zeichnet ein einzelnes Spalten-Byte (8 vertikale Pixel) an der aktuellen Position.
     */
    void drawColumn(uint8_t data);

private:
    uint8_t _address;
    uint8_t _sda;
    uint8_t _scl;

    void sendCommand(uint8_t command);
    void sendData(const uint8_t* data, size_t size);
};
