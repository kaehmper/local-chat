#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

/**
 * @class CustomDNSServer
 * @brief Ein hocheffizienter, leichtgewichtiger DNS-Server für das Captive Portal.
 *
 * Beantwortet alle DNS-Anfragen (Typ A) mit einer vordefinierten IP-Adresse (Redirect),
 * ohne externe Abhängigkeiten zu nutzen.
 */
class CustomDNSServer {
public:
    CustomDNSServer();
    ~CustomDNSServer() = default;

    /**
     * @brief Startet den DNS-Server auf dem angegebenen Port.
     * @param port Der UDP-Port (Standard: 53)
     * @param resolvedIP Die IP-Adresse, die für alle Anfragen zurückgegeben werden soll.
     * @return true bei Erfolg, andernfalls false.
     */
    bool begin(uint16_t port, const IPAddress& resolvedIP);

    /**
     * @brief Stoppt den DNS-Server.
     */
    void stop();

    /**
     * @brief Verarbeitet anstehende UDP-DNS-Pakete. Sollte in der Hauptschleife (loop) aufgerufen werden.
     */
    void process();

    /**
     * @brief Ruft die gesendeten Bytes seit dem letzten Aufruf ab und setzt den Zähler zurück.
     */
    uint32_t getAndResetBytesSent() {
        uint32_t bytes = _bytesSent;
        _bytesSent = 0;
        return bytes;
    }

    /**
     * @brief Ruft die empfangenen Bytes seit dem letzten Aufruf ab und setzt den Zähler zurück.
     */
    uint32_t getAndResetBytesReceived() {
        uint32_t bytes = _bytesReceived;
        _bytesReceived = 0;
        return bytes;
    }

private:
    WiFiUDP _udp;
    IPAddress _resolvedIP;
    uint16_t _port;
    bool _running;

    uint32_t _bytesSent;
    uint32_t _bytesReceived;

    // DNS Header Flags & Offsets
    static constexpr size_t DNS_PACKET_MAX_SIZE = 512;
    static constexpr uint8_t QR_MASK = 0x80; // Query/Response Flag
};
