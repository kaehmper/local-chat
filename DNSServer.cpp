#include "DNSServer.h"

CustomDNSServer::CustomDNSServer()
    : _resolvedIP(10, 10, 10, 1), _port(53), _running(false), _bytesSent(0), _bytesReceived(0) {}

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

    _bytesReceived += packetLen;

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

    // Sicherstellen, dass mindestens eine Frage vorhanden ist
    uint16_t qdcount = (packet[4] << 8) | packet[5];
    if (qdcount == 0) {
        return;
    }

    // Robustes Parsen des QNAME-Teils, um QTYPE und QCLASS zu bestimmen
    size_t qname_offset = 12;
    bool ended = false;
    bool compressed = false;
    while (qname_offset < static_cast<size_t>(bytesRead)) {
        uint8_t len = packet[qname_offset];
        if (len == 0) {
            ended = true;
            break; // Ende des QNAME
        }
        if ((len & 0xC0) == 0xC0) {
            // Kompressions-Pointer (2 Bytes lang)
            if (qname_offset + 1 >= static_cast<size_t>(bytesRead)) {
                return; // Ungültiges Paket (abgebrochener Kompressions-Pointer)
            }
            qname_offset += 2;
            ended = true;
            compressed = true;
            break;
        } else {
            // Label-Länge prüfen, um Überlauf zu verhindern
            if (qname_offset + 1 + len > static_cast<size_t>(bytesRead)) {
                return; // Ungültiges/Malformiertes Paket
            }
            qname_offset += 1 + len;
        }
    }

    if (!ended || qname_offset >= static_cast<size_t>(bytesRead)) {
        return; // Ungültiger QNAME oder Out-of-bounds
    }

    if (!compressed && packet[qname_offset] == 0) {
        qname_offset++; // Null-Byte überspringen, falls wir an einem unkomprimierten Null-Byte gestoppt haben
    }

    if (qname_offset + 4 > static_cast<size_t>(bytesRead)) {
        return; // Unvollständige Frage
    }

    uint16_t qtype = (packet[qname_offset] << 8) | packet[qname_offset + 1];
    uint16_t qclass = (packet[qname_offset + 2] << 8) | packet[qname_offset + 3];

    bool isTypeAOrAny = (qtype == 0x0001 || qtype == 0x00FF);   // TYPE A (IPv4) or TYPE ANY
    bool isClassINOrAny = (qclass == 0x0001 || qclass == 0x00FF); // CLASS IN (Internet) or CLASS ANY

    size_t question_end = qname_offset + 4;

    // Antwort-Puffer vorbereiten
    uint8_t response[DNS_PACKET_MAX_SIZE];

    // Nur Header und die erste Frage kopieren, alle zusätzlichen Sektionen (wie EDNS0) verwerfen!
    std::memcpy(response, packet, question_end);

    // Flags: QR=1 (Antwort), Opcode=0 (Standardabfrage), AA=1 (Autoritativ), TC=0, RD=1 (Rekursion erwünscht)
    response[2] = 0x85;
    // Flags: RA=1 (Rekursion verfügbar), Z=0, RCODE=0 (Kein Fehler)
    response[3] = 0x80;

    // QDCOUNT auf 1 festlegen (da wir nur die erste Frage kopieren)
    response[4] = 0x00;
    response[5] = 0x01;

    // ANCOUNT auf 0 initialisieren (wird auf 1 gesetzt, falls TYPE A/ANY)
    response[6] = 0x00;
    response[7] = 0x00;
    // NSCOUNT auf 0 setzen
    response[8] = 0x00;
    response[9] = 0x00;
    // ARCOUNT auf 0 setzen
    response[10] = 0x00;
    response[11] = 0x00;

    size_t offset = question_end;

    // Nur für TYPE A/ANY (IPv4) und CLASS IN/ANY (Internet) eine Antwort anhängen.
    // Andere Typen (z. B. AAAA) erhalten eine leere Antwort mit RCODE=0 (NOERROR),
    // um Timeouts auf modernen Geräten zu vermeiden.
    if (isTypeAOrAny && isClassINOrAny) {
        // ANCOUNT auf 1 setzen
        response[7] = 0x01;

        if (offset + 16 > DNS_PACKET_MAX_SIZE) {
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
    }

    // UDP-Paket zurücksenden
    if (_udp.beginPacket(_udp.remoteIP(), _udp.remotePort())) {
        _udp.write(response, offset);
        _udp.endPacket();
        _bytesSent += offset;
    }
}
