#include "TicTacToeManager.h"

// Statische Helfer zum Parsen von JSON-Werten im TicTacToeManager
static String unescapeJsonString(const String& val) {
    String result;
    result.reserve(val.length());
    for (size_t i = 0; i < val.length(); i++) {
        if (val[i] == '\\' && i + 1 < val.length()) {
            char next = val[i+1];
            switch (next) {
                case '"':  result += '"';  i++; break;
                case '\\': result += '\\'; i++; break;
                case '/':  result += '/';  i++; break;
                case 'b':  result += '\b'; i++; break;
                case 'f':  result += '\f'; i++; break;
                case 'n':  result += '\n'; i++; break;
                case 'r':  result += '\r'; i++; break;
                case 't':  result += '\t'; i++; break;
                default:   result += '\\'; break;
            }
        } else {
            result += val[i];
        }
    }
    return result;
}

static String getJsonValue(const String& json, const String& key) {
    String searchKey = "\"" + key + "\":\"";
    int start = json.indexOf(searchKey);
    if (start != -1) {
        start += searchKey.length();
        int end = -1;
        int curr = start;
        while (curr < (int)json.length()) {
            int nextQuote = json.indexOf('"', curr);
            if (nextQuote == -1) break;
            int backslashes = 0;
            int bsIndex = nextQuote - 1;
            while (bsIndex >= start && json[bsIndex] == '\\') {
                backslashes++;
                bsIndex--;
            }
            if (backslashes % 2 == 0) {
                end = nextQuote;
                break;
            }
            curr = nextQuote + 1;
        }
        if (end != -1) {
            return unescapeJsonString(json.substring(start, end));
        }
    } else {
        searchKey = "\"" + key + "\":";
        start = json.indexOf(searchKey);
        if (start != -1) {
            start += searchKey.length();
            int endCom = json.indexOf(",", start);
            int endObj = json.indexOf("}", start);
            int end = -1;
            if (endCom != -1 && endObj != -1) {
                end = (endCom < endObj) ? endCom : endObj;
            } else if (endCom != -1) {
                end = endCom;
            } else if (endObj != -1) {
                end = endObj;
            }
            if (end != -1) {
                String val = json.substring(start, end);
                val.replace("\"", "");
                val.trim();
                return val;
            } else {
                String val = json.substring(start);
                val.replace("\"", "");
                val.trim();
                return val;
            }
        }
    }
    return "";
}

bool TicTacToeManager::isValidUid(const String& uid) {
    if (uid.length() != 4) return false;
    for (int i = 0; i < 4; ++i) {
        char c = uid[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool TicTacToeManager::buildSecureMessage(const String& fromUid,
                                         const String& message,
                                         String& outTargetUid,
                                         String& outSecureJson) {
    outTargetUid = getJsonValue(message, "to");
    if (outTargetUid.length() != 4) return false;
    outTargetUid.toUpperCase();

    String cmd = getJsonValue(message, "cmd");
    String cellVal = getJsonValue(message, "cell");

    outSecureJson = "{\"type\":\"ttt\",\"from\":\"" + fromUid + "\",\"to\":\"" + outTargetUid + "\",\"cmd\":\"" + cmd + "\"";
    if (cellVal.length() > 0) {
        outSecureJson += ",\"cell\":" + cellVal;
    }
    outSecureJson += "}";

    return true;
}
