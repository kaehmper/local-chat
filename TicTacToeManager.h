#pragma once

#include <Arduino.h>

/**
 * @class TicTacToeManager
 * @brief Behandelt die Tic-Tac-Toe Signalisierungsnachrichten und Validierungen.
 *
 * Sichert die Kommunikation gegen Spoofing-Angriffe, indem Absender-IDs
 * erzwungen und Pakete strukturiert neu aufgebaut werden.
 */
class TicTacToeManager {
public:
    TicTacToeManager() = default;

    /**
     * @brief Rekonstruiert eine Tic-Tac-Toe-Nachricht sicher, um Spoofing zu verhindern.
     * @param fromUid Die verifizierte UID des Absenders.
     * @param message Die rohe JSON-Nachricht vom WebSocket.
     * @param outTargetUid Ausgabe-Parameter für die Ziel-UID.
     * @param outSecureJson Ausgabe-Parameter für das bereinigte, sichere JSON-Paket.
     * @return true bei Erfolg, false bei ungültigen Parametern.
     */
    static bool buildSecureMessage(const String& fromUid,
                                   const String& message,
                                   String& outTargetUid,
                                   String& outSecureJson);

    /**
     * @brief Validiert, ob eine UID das korrekte 4-stellige Hex-Format besitzt.
     */
    static bool isValidUid(const String& uid);
};
