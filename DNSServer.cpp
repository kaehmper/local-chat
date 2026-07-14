#include "DNSServer.h"

CustomDNSServer::CustomDNSServer() 
    : _resolvedIP(10, 10, 10, 1), _port(53), _running(false) {}

bool CustomDNSServer::begin(uint16_t port, const IPAddress& resolvedIP) {
    _port = port;
    _resolvedIP = resolvedIP;
    _running = _udp.begin(_port) != 0;
    return _running;
}

void CustomDNSServer::stop() {
    if (_running) {
        _udp.stop();
        _running = false;
    }
}

void CustomDNSServer::process() {
    if (!_running) return;

    int packetLen = _udp.parsePacket();
    if (packetLen <= 0) return;

    // DNS-Pakete müssen mindestens den Header enthalten (12 Bytes) und dürfen das Limit nicht überschreiten
    if (packetLen < 12 || static_cast<size_t>(packetLen) > DNS_PACKET_MAX_SIZE) {
        _udp.flush();
        return;
    }

    uint8_t packet[DNS_PACKET_MAX_SIZE];
    int bytesRead = _udp.read(packet, DNS_PACKET_MAX_SIZE);
    if (bytesRead < 12) return;

    // Nur Anfragen (QR = 0) bearbeiten. Das QR-Bit befindet sich im Byte 2, Bit 7
    if ((packet[2] & QR_MASK) != 0) {
        return;
    }

    // Antwort-Puffer vorbereiten
    uint8_t response[DNS_PACKET_MAX_SIZE];
    
    // Header kopieren und anpassen
    std::memcpy(response, packet, bytesRead);

    // Flags: QR=1 (Antwort), Opcode=0 (Standardabfrage), AA=1 (Autoritativ), TC=0, RD=1 (Rekursion erwünscht)
    response[2] = 0x85; 
    // Flags: RA=1 (Rekursion verfügbar), Z=0, RCODE=0 (Kein Fehler)
    response[3] = 0x80;

    // Anzahl Fragen (QDCOUNT) bleibt gleich wie in der Anfrage (meistens 1)
    // Anzahl Antworten (ANCOUNT) auf 1 setzen (high byte = 0x00, low byte = 0x01)
    response[6] = 0x00;
    response[7] = 0x01;
    // NSCOUNT (Nameserver-Einträge) auf 0 setzen
    response[8] = 0x00;
    response[9] = 0x00;
    // ARCOUNT (Zusätzliche Einträge) auf 0 setzen
    response[10] = 0x00;
    response[11] = 0x00;

    // Die Antwort-Ressourcen-Eintragung anhängen
    // Wir verweisen mit einem DNS-Namens-Pointer auf den angefragten Namen (Offset 12 = 0x0C)
    size_t offset = bytesRead;
    
    if (offset + 16 > DNS_PACKET_MAX_SIZE) {
        // Schutz vor Pufferüberlauf falls das Anfragepaket zu groß war
        return;
    }

    // Name-Pointer: 0xC000 | 12 (0x0C) -> 0xC00C
    response[offset++] = 0xC0;
    response[offset++] = 0x0C;

    // TYPE: A (IPv4-Adresse, Wert 1)
    response[offset++] = 0x00;
    response[offset++] = 0x01;

    // CLASS: IN (Internet, Wert 1)
    response[offset++] = 0x00;
    response[offset++] = 0x01;

    // TTL (Time To Live): 60 Sekunden (0x0000003C)
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    response[offset++] = 0x3C;

    // RDLENGTH (Länge der IP-Daten): 4 Bytes
    response[offset++] = 0x00;
    response[offset++] = 0x04;

    // RDATA: Die konfigurierte IP-Adresse
    response[offset++] = _resolvedIP[0];
    response[offset++] = _resolvedIP[1];
    response[offset++] = _resolvedIP[2];
    response[offset++] = _resolvedIP[3];

    // UDP-Paket zurücksenden
    if (_udp.beginPacket(_udp.remoteIP(), _udp.remotePort())) {
        _udp.write(response, offset);
        _udp.endPacket();
    }
}
