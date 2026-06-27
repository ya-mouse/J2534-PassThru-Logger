#include "LogParser.h"
#include "Logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

//
// === String helpers ===
//

const char* LogParser::stripWhitespace(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        p++;
    return p;
}

const char* LogParser::findChar(const char *p, const char *end, char c) {
    while (p < end && *p != c)
        p++;
    return p;
}

const char* LogParser::findClosingBrace(const char *p, const char *end, char open, char close) {
    int depth = 1;
    while (p < end && depth > 0) {
        if (*p == open) depth++;
        else if (*p == close) depth--;
        if (depth > 0) p++;
    }
    return p;
}

int LogParser::strEqual(const char *a, int aLen, const char *b) {
    int bLen = (int)strlen(b);
    if (aLen != bLen) return 0;
    return memcmp(a, b, bLen) == 0;
}

int LogParser::startsWith(const char *p, const char *end, const char *prefix) {
    int pLen = (int)(end - p);
    int prefixLen = (int)strlen(prefix);
    if (pLen < prefixLen) return 0;
    if (memcmp(p, prefix, prefixLen) == 0) return prefixLen;
    return 0;
}

unsigned long LogParser::parseHex(const char *p, int maxLen) {
    unsigned long val = 0;
    for (int i = 0; i < maxLen && p[i]; i++) {
        char c = p[i];
        if (c >= '0' && c <= '9')      val = val * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
        else break;
    }
    return val;
}

unsigned long LogParser::parseDecimal(const char *p, int maxLen) {
    unsigned long val = 0;
    for (int i = 0; i < maxLen && p[i]; i++) {
        if (p[i] >= '0' && p[i] <= '9')
            val = val * 10 + (p[i] - '0');
        else break;
    }
    return val;
}

//
// === Lookup tables ===
//

unsigned long LogParser::lookupError(const char *name, int len) {
    struct { const char *n; unsigned long v; } table[] = {
        {"STATUS_NOERROR", STATUS_NOERROR},
        {"ERR_NOT_SUPPORTED", ERR_NOT_SUPPORTED},
        {"ERR_INVALID_CHANNEL_ID", ERR_INVALID_CHANNEL_ID},
        {"ERR_INVALID_PROTOCOL_ID", ERR_INVALID_PROTOCOL_ID},
        {"ERR_NULL_PARAMETER", ERR_NULL_PARAMETER},
        {"ERR_INVALID_IOCTL_VALUE", ERR_INVALID_IOCTL_VALUE},
        {"ERR_INVALID_FLAGS", ERR_INVALID_FLAGS},
        {"ERR_FAILED", ERR_FAILED},
        {"ERR_DEVICE_NOT_CONNECTED", ERR_DEVICE_NOT_CONNECTED},
        {"ERR_TIMEOUT", ERR_TIMEOUT},
        {"ERR_INVALID_MSG", ERR_INVALID_MSG},
        {"ERR_EXCEEDED_LIMIT", ERR_EXCEEDED_LIMIT},
        {"ERR_DEVICE_IN_USE", ERR_DEVICE_IN_USE},
        {"ERR_INVALID_IOCTL_ID", ERR_INVALID_IOCTL_ID},
        {"ERR_BUFFER_EMPTY", ERR_BUFFER_EMPTY},
        {"ERR_BUFFER_FULL", ERR_BUFFER_FULL},
        {"ERR_BUFFER_OVERFLOW", ERR_BUFFER_OVERFLOW},
        {"ERR_CHANNEL_IN_USE", ERR_CHANNEL_IN_USE},
        {"ERR_INVALID_FILTER_ID", ERR_INVALID_FILTER_ID},
        {"ERR_NO_FLOW_CONTROL", ERR_NO_FLOW_CONTROL},
        {"ERR_INVALID_BAUDRATE", ERR_INVALID_BAUDRATE},
        {"ERR_INVALID_DEVICE_ID", ERR_INVALID_DEVICE_ID},
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if (strEqual(name, len, table[i].n))
            return table[i].v;
    }
    return 0xFFFFFFFF;
}

unsigned long LogParser::lookupProtocol(const char *name, int len) {
    struct { const char *n; unsigned long v; } table[] = {
        {"J1850VPW", J2534_J1850VPW},
        {"J1850PWM", J2534_J1850PWM},
        {"ISO9141", J2534_ISO9141},
        {"ISO14230", J2534_ISO14230},
        {"CAN", J2534_CAN},
        {"ISO15765", J2534_ISO15765},
        {"SCI_A_ENGINE", J2534_SCI_A_ENGINE},
        {"SCI_A_TRANS", J2534_SCI_A_TRANS},
        {"SCI_B_ENGINE", J2534_SCI_B_ENGINE},
        {"SCI_B_TRANS", J2534_SCI_B_TRANS},
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if (strEqual(name, len, table[i].n))
            return table[i].v;
    }
    // Try hex parse (e.g., for FD protocols)
    if (len > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X'))
        return parseHex(name + 2, len - 2);
    return 0;
}

unsigned long LogParser::lookupIoctl(const char *name, int len) {
    struct { const char *n; unsigned long v; } table[] = {
        {"GET_CONFIG", GET_CONFIG},
        {"SET_CONFIG", SET_CONFIG},
        {"READ_VBATT", READ_VBATT},
        {"FIVE_BAUD_INIT", FIVE_BAUD_INIT},
        {"FAST_INIT", FAST_INIT},
        {"CLEAR_TX_BUFFER", CLEAR_TX_BUFFER},
        {"CLEAR_RX_BUFFER", CLEAR_RX_BUFFER},
        {"CLEAR_PERIODIC_MSGS", CLEAR_PERIODIC_MSGS},
        {"CLEAR_MSG_FILTERS", CLEAR_MSG_FILTERS},
        {"CLEAR_FUNCT_MSG_LOOKUP_TABLE", CLEAR_FUNCT_MSG_LOOKUP_TABLE},
        {"ADD_TO_FUNCT_MSG_LOOKUP_TABLE", ADD_TO_FUNCT_MSG_LOOKUP_TABLE},
        {"DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE", DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE},
        {"READ_PROG_VOLTAGE", READ_PROG_VOLTAGE},
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if (strEqual(name, len, table[i].n))
            return table[i].v;
    }
    return 0;
}

unsigned long LogParser::lookupConnectFlag(const char *name, int len) {
    struct { const char *n; unsigned long v; } table[] = {
        {"CAN_29BIT_ID", CAN_29BIT_ID},
        {"ISO9141_NO_CHECKSUM", ISO9141_NO_CHECKSUM},
        {"CAN_ID_BOTH", CAN_ID_BOTH},
        {"ISO9141_K_LINE_ONLY", ISO9141_K_LINE_ONLY},
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if (strEqual(name, len, table[i].n))
            return table[i].v;
    }
    return 0;
}

unsigned long LogParser::lookupTxFlag(const char *name, int len) {
    struct { const char *n; unsigned long v; } table[] = {
        {"ISO15765_FRAME_PAD", ISO15765_FRAME_PAD},
        {"WAIT_P3_MIN_ONLY", WAIT_P3_MIN_ONLY},
        {"ISO15765_ADDR_TYPE", ISO15765_ADDR_TYPE},
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if (strEqual(name, len, table[i].n))
            return table[i].v;
    }
    return 0;
}

unsigned long LogParser::lookupFilterType(const char *name, int len) {
    struct { const char *n; unsigned long v; } table[] = {
        {"PASS_FILTER", PASS_FILTER},
        {"BLOCK_FILTER", BLOCK_FILTER},
        {"FLOW_CONTROL_FILTER", FLOW_CONTROL_FILTER},
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if (strEqual(name, len, table[i].n))
            return table[i].v;
    }
    return 0;
}

unsigned long LogParser::lookupConfigParam(const char *name, int len) {
    struct { const char *n; unsigned long v; } table[] = {
        {"DATA_RATE", DATA_RATE},
        {"LOOPBACK", LOOPBACK},
        {"NODE_ADDRESS", NODE_ADDRESS},
        {"NETWORK_LINE", NETWORK_LINE},
        {"BIT_SAMPLE_POINT", BIT_SAMPLE_POINT},
        {"SYNC_JUMP_WIDTH", SYNC_JUMP_WIDTH},
        {"ISO15765_BS", ISO15765_BS},
        {"ISO15765_STMIN", ISO15765_STMIN},
        {"ISO15765_BS_TX", ISO15765_BS_TX},
        {"ISO15765_STMIN_TX", ISO15765_STMIN_TX},
        {"ISO15765_WFT_MAX", ISO15765_WFT_MAX},
        {"CAN_MIXED_FORMAT", CAN_MIXED_FORMAT},
        {"ISO15765_PAD_VALUE", ISO15765_PAD_VALUE},
        {"FD_CAN_DATA_PHASE_RATE", FD_CAN_DATA_PHASE_RATE},
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if (strEqual(name, len, table[i].n))
            return table[i].v;
    }
    return 0;
}

//
// === LogParser ===
//

LogParser::LogParser()
    : events_(NULL), eventCount_(0), eventCapacity_(0)
{
}

LogParser::~LogParser() {
    clear();
}

void LogParser::clear() {
    if (events_) {
        free(events_);
        events_ = NULL;
    }
    eventCount_ = 0;
    eventCapacity_ = 0;
}

bool LogParser::ensureCapacity(int needed) {
    if (needed <= eventCapacity_) return true;
    int newCap = eventCapacity_ == 0 ? 256 : eventCapacity_;
    while (newCap < needed) newCap *= 2;
    ReplayEvent *newEvents = (ReplayEvent *)realloc(events_, newCap * sizeof(ReplayEvent));
    if (!newEvents) return false;
    events_ = newEvents;
    eventCapacity_ = newCap;
    return true;
}

const ReplayEvent* LogParser::getEvent(int index) const {
    if (index < 0 || index >= eventCount_) return NULL;
    return &events_[index];
}

bool LogParser::load(const char *filePath) {
    clear();
    if (!filePath || !filePath[0]) {
        g_logger.verbose("LogParser: empty file path");
        return false;
    }
    if (!parseJsonFile(filePath)) {
        g_logger.verbose("LogParser: failed to parse '%s'", filePath);
        return false;
    }
    g_logger.verbose("LogParser: loaded %d events from '%s'", eventCount_, filePath);
    return true;
}

//
// === JSON parsing ===
//
// Minimal state machine to extract entries from the known JSON structure.
// Handles JSON string escape sequences including \uXXXX.

// Decode a JSON string (between quotes) into buf. Returns decoded length.
static int jsonDecodeString(const char *src, int srcLen, char *buf, int bufSize) {
    int out = 0;
    for (int i = 0; i < srcLen && out < bufSize - 1; i++) {
        if (src[i] == '\\' && i + 1 < srcLen) {
            i++;
            switch (src[i]) {
            case '"':  buf[out++] = '"'; break;
            case '\\': buf[out++] = '\\'; break;
            case '/':  buf[out++] = '/'; break;
            case 'n':  buf[out++] = '\n'; break;
            case 'r':  buf[out++] = '\r'; break;
            case 't':  buf[out++] = '\t'; break;
            case 'u': {
                // \uXXXX — decode first 4 hex chars
                if (i + 4 < srcLen) {
                    unsigned long cp = LogParser::parseHex(src + i + 1, 4);
                    i += 4;
                    if (cp < 0x80) {
                        buf[out++] = (char)cp;
                    } else if (cp < 0x800) {
                        buf[out++] = (char)(0xC0 | (cp >> 6));
                        if (out < bufSize - 1) buf[out++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[out++] = (char)(0xE0 | (cp >> 12));
                        if (out < bufSize - 1) buf[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        if (out < bufSize - 1) buf[out++] = (char)(0x80 | (cp & 0x3F));
                    }
                } else {
                    buf[out++] = '?';
                }
                break;
            }
            default:
                buf[out++] = src[i];
                break;
            }
        } else {
            buf[out++] = src[i];
        }
    }
    buf[out] = '\0';
    return out;
}

// Find value of a named key in a JSON object. p points after the opening '{'.
// Returns pointer to the value start (past the ':'). For strings, points past opening '"'.
static const char* jsonFindKey(const char *p, const char *end, const char *key) {
    while (p < end) {
        p = LogParser::stripWhitespace(p, end);
        if (p >= end || *p == '}') break;
        if (*p == ',') { p++; continue; }
        if (*p != '"') { p++; continue; }
        // Found a string — check if it's our key
        p++; // skip opening quote
        const char *keyStart = p;
        p = LogParser::findChar(p, end, '"');
        if (p >= end) break;
        int foundLen = (int)(p - keyStart);
        p++; // skip closing quote
        if (LogParser::strEqual(keyStart, foundLen, key)) {
            // Skip ':'
            p = LogParser::stripWhitespace(p, end);
            if (p < end && *p == ':') p++;
            p = LogParser::stripWhitespace(p, end);
            return p;
        }
        // Skip the value
        p = LogParser::stripWhitespace(p, end);
        if (p >= end) break;
        if (*p == ',') { p++; continue; }
        if (*p == ':') {
            p++;
            p = LogParser::stripWhitespace(p, end);
            if (p >= end) break;
            if (*p == '"') {
                p++;
                // Skip string value, handling escapes
                while (p < end) {
                    if (*p == '\\') { p += 2; continue; }
                    if (*p == '"') { p++; break; }
                    p++;
                }
            } else if (*p == '{') {
                p = LogParser::findClosingBrace(p + 1, end, '{', '}');
                if (p < end) p++;
            } else if (*p == '[') {
                p = LogParser::findClosingBrace(p + 1, end, '[', ']');
                if (p < end) p++;
            } else {
                // Number, true, false, null
                while (p < end && *p != ',' && *p != '}' && *p != ' '
                       && *p != '\n' && *p != '\r')
                    p++;
            }
        }
    }
    return NULL;
}

bool LogParser::parseJsonFile(const char *filePath) {
    // Read entire file
    HANDLE hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize > 50 * 1024 * 1024) { // max 50MB
        CloseHandle(hFile);
        return false;
    }

    char *fileBuf = (char *)malloc(fileSize + 1);
    if (!fileBuf) {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, fileBuf, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        free(fileBuf);
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    fileBuf[fileSize] = '\0';

    // Find the "entries" array
    const char *entriesKey = strstr(fileBuf, "\"entries\"");
    if (!entriesKey) {
        free(fileBuf);
        return false;
    }

    // Find the '[' after "entries"
    const char *p = entriesKey + 9; // strlen("\"entries\"")
    while (*p && *p != '[') p++;
    if (!*p) {
        free(fileBuf);
        return false;
    }
    p++; // skip '['

    const char *fileEnd = fileBuf + fileSize;

    // Parse each JSON object in the array
    while (p < fileEnd) {
        p = stripWhitespace(p, fileEnd);
        if (p >= fileEnd || *p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') { p++; continue; }

        // Find the closing brace of this object
        const char *objStart = p;
        const char *objEnd = findClosingBrace(p + 1, fileEnd, '{', '}');
        if (objEnd >= fileEnd) break;

        parseEntry(objStart, (int)(objEnd - objStart + 1));

        p = objEnd + 1;
    }

    free(fileBuf);
    return eventCount_ > 0;
}

bool LogParser::parseEntry(const char *jsonObj, int jsonLen) {
    const char *end = jsonObj + jsonLen;

    // Extract "text" field
    const char *textVal = jsonFindKey(jsonObj + 1, end - 1, "text");
    if (!textVal) return false;

    // Decode the text string
    if (*textVal != '"') return false;
    textVal++; // skip opening quote
    const char *textEnd = textVal;
    while (textEnd < end) {
        if (*textEnd == '\\' && textEnd + 1 < end) { textEnd += 2; continue; }
        if (*textEnd == '"') break;
        textEnd++;
    }

    char text[2048];
    jsonDecodeString(textVal, (int)(textEnd - textVal), text, sizeof(text));

    // Extract "count" field (number)
    unsigned long count = 1;
    const char *countVal = jsonFindKey(jsonObj + 1, end - 1, "count");
    if (countVal) {
        count = parseDecimal(countVal, 10);
        if (count == 0) count = 1;
    }

    // Parse the call text
    ReplayEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.count = count;

    if (!parseCallText(text, ev))
        return false;

    // Add to events array
    if (!ensureCapacity(eventCount_ + 1))
        return false;
    memcpy(&events_[eventCount_], &ev, sizeof(ev));
    eventCount_++;
    return true;
}

//
// === Text parsing ===
//

bool LogParser::parseCallText(const char *text, ReplayEvent &ev) {
    // Skip "Client: ..." and "Driver: ..." lines
    if (startsWith(text, text + strlen(text), "Client:") > 0 ||
        startsWith(text, text + strlen(text), "Driver:") > 0)
        return false;

    const char *p = text;
    int textLen = (int)strlen(text);

    // Find the " -> " separator (in original log, may appear as " -> " after decode = " -> ")
    // Or just " -> "
    const char *arrow = strstr(p, " -> ");
    const char *end = p + textLen;

    if (!arrow) return false;

    // Parse return code after " -> "
    const char *retStart = arrow + 4;
    retStart = stripWhitespace(retStart, end);
    int retLen = (int)(end - retStart);
    // Trim trailing whitespace
    while (retLen > 0 && (retStart[retLen-1] == ' ' || retStart[retLen-1] == '\n'
                          || retStart[retLen-1] == '\r'))
        retLen--;

    ev.returnCode = lookupError(retStart, retLen);

    // Parse the function call before " -> "
    // Format: FunctionName(arg1, arg2, ...) -> returnCode
    const char *callEnd = arrow;

    // Match function name
    int openMatch = startsWith(p, callEnd, "PassThruOpen(");
    int closeMatch = startsWith(p, callEnd, "PassThruClose(");
    int connectMatch = startsWith(p, callEnd, "PassThruConnect(");
    int disconnectMatch = startsWith(p, callEnd, "PassThruDisconnect(");
    int writeMatch = startsWith(p, callEnd, "PassThruWriteMsgs(");
    int readMatch = startsWith(p, callEnd, "PassThruReadMsgs(");
    int ioctlMatch = startsWith(p, callEnd, "PassThruIoctl(");
    int filterStartMatch = startsWith(p, callEnd, "PassThruStartMsgFilter(");
    int filterStopMatch = startsWith(p, callEnd, "PassThruStopMsgFilter(");
    int readVerMatch = startsWith(p, callEnd, "PassThruReadVersion(");

    // Find opening paren and args
    const char *parenOpen = NULL;
    if (openMatch) { ev.type = CALL_OPEN; parenOpen = p + openMatch - 1; }
    else if (closeMatch) { ev.type = CALL_CLOSE; parenOpen = p + closeMatch - 1; }
    else if (connectMatch) { ev.type = CALL_CONNECT; parenOpen = p + connectMatch - 1; }
    else if (disconnectMatch) { ev.type = CALL_DISCONNECT; parenOpen = p + disconnectMatch - 1; }
    else if (writeMatch) { ev.type = CALL_WRITE; parenOpen = p + writeMatch - 1; }
    else if (readMatch) { ev.type = CALL_READ; parenOpen = p + readMatch - 1; }
    else if (ioctlMatch) { ev.type = CALL_IOCTL; parenOpen = p + ioctlMatch - 1; }
    else if (filterStartMatch) { ev.type = CALL_FILTER_START; parenOpen = p + filterStartMatch - 1; }
    else if (filterStopMatch) { ev.type = CALL_FILTER_STOP; parenOpen = p + filterStopMatch - 1; }
    else if (readVerMatch) { ev.type = CALL_READ_VERSION; parenOpen = p + readVerMatch - 1; }
    else return false;

    // Parse the arguments between parens
    const char *argsStart = parenOpen + 1;
    const char *argsEnd = callEnd;
    // The closing paren should be just before the arrow
    if (argsEnd > argsStart && *(argsEnd - 1) == ')')
        argsEnd--;

    // Parse based on call type
    switch (ev.type) {
    case CALL_OPEN: {
        // PassThruOpen(NULL, 1232) or PassThruOpen("name", deviceId)
        const char *a = stripWhitespace(argsStart, argsEnd);
        // First arg: device name or NULL
        if (startsWith(a, argsEnd, "NULL") > 0) {
            strcpy(ev.openName, "");
            const char *comma = findChar(a, argsEnd, ',');
            if (comma < argsEnd) {
                comma++;
                comma = stripWhitespace(comma, argsEnd);
                ev.handle = parseDecimal(comma, (int)(argsEnd - comma));
            }
        } else if (*a == '"') {
            a++;
            const char *qEnd = findChar(a, argsEnd, '"');
            int nameLen = (int)(qEnd - a);
            if (nameLen > 0 && nameLen < (int)sizeof(ev.openName)) {
                memcpy(ev.openName, a, nameLen);
                ev.openName[nameLen] = '\0';
            }
            const char *comma = findChar(qEnd, argsEnd, ',');
            if (comma < argsEnd) {
                comma++;
                comma = stripWhitespace(comma, argsEnd);
                ev.handle = parseDecimal(comma, (int)(argsEnd - comma));
            }
        }
        break;
    }

    case CALL_CLOSE: {
        // PassThruClose(deviceId)
        ev.handle = parseDecimal(argsStart, (int)(argsEnd - argsStart));
        break;
    }

    case CALL_CONNECT: {
        // PassThruConnect(deviceId, ISO15765, CAN_ID_BOTH, 500000, channelId)
        const char *a = stripWhitespace(argsStart, argsEnd);
        // deviceId
        ev.handle = parseDecimal(a, 20);
        const char *comma = findChar(a, argsEnd, ',');
        if (comma >= argsEnd) break;
        a = stripWhitespace(comma + 1, argsEnd);
        // Protocol name
        const char *nextComma = findChar(a, argsEnd, ',');
        if (nextComma >= argsEnd) break;
        ev.protocolId = lookupProtocol(a, (int)(nextComma - a));
        a = stripWhitespace(nextComma + 1, argsEnd);
        // Flags name (could be comma-separated in future, handle single for now)
        nextComma = findChar(a, argsEnd, ',');
        if (nextComma >= argsEnd) break;
        ev.connectFlags = lookupConnectFlag(a, (int)(nextComma - a));
        a = stripWhitespace(nextComma + 1, argsEnd);
        // Baud rate
        nextComma = findChar(a, argsEnd, ',');
        if (nextComma >= argsEnd) break;
        ev.baudRate = parseDecimal(a, (int)(nextComma - a));
        a = stripWhitespace(nextComma + 1, argsEnd);
        // Channel ID (output, but logged as the assigned value)
        ev.outChannelId = parseDecimal(a, (int)(argsEnd - a));
        break;
    }

    case CALL_DISCONNECT: {
        // PassThruDisconnect(channelId)
        ev.handle = parseDecimal(argsStart, (int)(argsEnd - argsStart));
        break;
    }

    case CALL_WRITE:
    case CALL_READ: {
        // PassThruWriteMsgs(channelId, [msgs], numIn=>numOut, timeout)
        // PassThruReadMsgs(channelId, [msgs], numIn=>numOut, timeout)
        const char *a = stripWhitespace(argsStart, argsEnd);
        // channelId
        ev.handle = parseDecimal(a, 20);
        const char *comma = findChar(a, argsEnd, ',');
        if (comma >= argsEnd) break;
        a = stripWhitespace(comma + 1, argsEnd);
        // Parse message array [...]
        if (*a == '[') {
            const char *arrEnd = findClosingBrace(a + 1, argsEnd, '[', ']');
            if (arrEnd < argsEnd) {
                ev.numMsgs = (unsigned long)parseMessages(a + 1, arrEnd, ev);
                arrEnd++; // skip ']'
                // Skip comma after ]
                const char *nextComma = findChar(arrEnd, argsEnd, ',');
                if (nextComma < argsEnd) a = stripWhitespace(nextComma + 1, argsEnd);
            }
        } else {
            // No message array, skip to next arg
            const char *nextComma = findChar(a, argsEnd, ',');
            if (nextComma < argsEnd) a = stripWhitespace(nextComma + 1, argsEnd);
        }

        // Parse numIn=>numOut (the => is encoded as > in JSON, decoded to >)
        // Format: "1=>1" or "1=>0"
        const char *eqGt = strstr(a, "=>");
        if (eqGt) {
            ev.numMsgsIn = parseDecimal(a, (int)(eqGt - a));
            ev.numMsgsOut = parseDecimal(eqGt + 2, 20);
            // Find next comma
            const char *nextComma = findChar(a, argsEnd, ',');
            if (nextComma < argsEnd) {
                nextComma = stripWhitespace(nextComma + 1, argsEnd);
                ev.timeout = parseDecimal(nextComma, (int)(argsEnd - nextComma));
            }
        }
        break;
    }

    case CALL_IOCTL: {
        // PassThruIoctl(handle, IOCTL_NAME, inputOrNull, outputValue)
        const char *a = stripWhitespace(argsStart, argsEnd);
        // handle
        ev.handle = parseDecimal(a, 20);
        const char *comma = findChar(a, argsEnd, ',');
        if (comma >= argsEnd) break;
        a = stripWhitespace(comma + 1, argsEnd);
        // ioctl name or UNK(hex)
        const char *nextComma = findChar(a, argsEnd, ',');
        int ioctlNameLen = (int)(nextComma < argsEnd ? nextComma - a : argsEnd - a);
        if (startsWith(a, a + ioctlNameLen, "UNK(") > 0) {
            // UNK(0xHEX)
            const char *hStart = a + 4;
            if (hStart < nextComma && (*hStart == '0' || *hStart == '1')) {
                if (*hStart == '0' && hStart + 1 < nextComma &&
                    (hStart[1] == 'x' || hStart[1] == 'X'))
                    ev.ioctlId = parseHex(hStart + 2, (int)(nextComma - hStart - 2));
                else
                    ev.ioctlId = parseDecimal(hStart, (int)(nextComma - hStart));
            }
            ev.ioctlIdKnown = false;
            const char *closeP = findChar(a, nextComma, ')');
            if (closeP < nextComma) {
                if (*hStart == '0' && hStart[1] == 'x') {
                    // Already handled
                } else {
                    ev.ioctlId = parseHex(hStart, (int)(closeP - hStart));
                }
            }
        } else {
            ev.ioctlId = lookupIoctl(a, ioctlNameLen);
            ev.ioctlIdKnown = (ev.ioctlId != 0 || strEqual(a, ioctlNameLen, "CLEAR_TX_BUFFER") ||
                               strEqual(a, ioctlNameLen, "CLEAR_RX_BUFFER") ||
                               strEqual(a, ioctlNameLen, "CLEAR_MSG_FILTERS") ||
                               strEqual(a, ioctlNameLen, "CLEAR_PERIODIC_MSGS"));
        }
        if (nextComma < argsEnd) {
            a = stripWhitespace(nextComma + 1, argsEnd);
        } else {
            break;
        }
        // Input: NULL or [[param, value], ...] or a SCONFIG_LIST representation
        nextComma = findChar(a, argsEnd, ',');
        if (startsWith(a, argsEnd, "NULL") == 4) {
            ev.hasInput = false;
        } else if (*a == '[') {
            // SCONFIG_LIST: [[paramName, value], [paramName, value], ...]
            // Parse config pairs
            const char *sqEnd = findClosingBrace(a + 1, argsEnd > nextComma ? nextComma : argsEnd, '[', ']');
            ev.hasInput = true;
            // Store config params as ioctlIn data (SCONFIG_LIST format)
            SCONFIG *configs = (SCONFIG *)ev.ioctlIn;
            int numConfigs = 0;
            const char *sp = a + 1; // skip outer [
            const char *spEnd = nextComma < argsEnd ? nextComma : sqEnd + 1;
            while (sp < spEnd) {
                sp = LogParser::stripWhitespace(sp, spEnd);
                if (*sp != '[') { sp++; continue; }
                sp++; // skip inner [
                sp = LogParser::stripWhitespace(sp, spEnd);
                // Param name
                const char *innerComma = LogParser::findChar(sp, spEnd, ',');
                if (innerComma >= spEnd) break;
                unsigned long param = LogParser::lookupConfigParam(sp, (int)(innerComma - sp));
                sp = LogParser::stripWhitespace(innerComma + 1, spEnd);
                // Value
                unsigned long val = LogParser::parseDecimal(sp, 20);
                if (numConfigs < 32) {
                    configs[numConfigs].Parameter = param;
                    configs[numConfigs].Value = val;
                    numConfigs++;
                }
                // Skip to closing ]
                const char *closeB = LogParser::findChar(sp, spEnd, ']');
                if (closeB < spEnd) sp = closeB + 1;
                else break;
            }
            ev.ioctlInSize = numConfigs * sizeof(SCONFIG);
        } else {
            ev.hasInput = false;
        }

        // Move past input to output
        if (nextComma < argsEnd) {
            a = stripWhitespace(nextComma + 1, argsEnd);
        }
        // Output: number, NULL, or empty
        if (startsWith(a, argsEnd, "NULL") == 4 || a >= argsEnd) {
            ev.hasOutput = false;
        } else {
            // Numeric value
            ev.hasOutput = true;
            unsigned long outVal = parseDecimal(a, 20);
            memcpy(ev.ioctlOut, &outVal, sizeof(outVal));
            ev.ioctlOutSize = sizeof(outVal);
        }
        break;
    }

    case CALL_FILTER_START: {
        // PassThruStartMsgFilter(channelId, FILTER_TYPE, {mask}, {pattern}, {flow}, filterId)
        // or without flow control for PASS/BLOCK
        const char *a = stripWhitespace(argsStart, argsEnd);
        // channelId
        ev.handle = parseDecimal(a, 20);
        const char *comma = findChar(a, argsEnd, ',');
        if (comma >= argsEnd) break;
        a = stripWhitespace(comma + 1, argsEnd);
        // Filter type
        comma = findChar(a, argsEnd, ',');
        if (comma >= argsEnd) break;
        ev.filterType = lookupFilterType(a, (int)(comma - a));
        a = stripWhitespace(comma + 1, argsEnd);
        // Mask message {...}
        if (*a == '{') {
            const char *braceEnd = findClosingBrace(a + 1, argsEnd, '{', '}');
            if (braceEnd < argsEnd) {
                parseSingleMessage(a + 1, braceEnd, ev.maskMsg);
                a = stripWhitespace(braceEnd + 1, argsEnd);
                if (*a == ',') a = stripWhitespace(a + 1, argsEnd);
            }
        }
        // Pattern message {...}
        if (*a == '{') {
            const char *braceEnd = findClosingBrace(a + 1, argsEnd, '{', '}');
            if (braceEnd < argsEnd) {
                parseSingleMessage(a + 1, braceEnd, ev.patternMsg);
                a = stripWhitespace(braceEnd + 1, argsEnd);
                if (*a == ',') a = stripWhitespace(a + 1, argsEnd);
            }
        }
        // Flow control message {...} (only for FLOW_CONTROL_FILTER)
        if (*a == '{') {
            ev.hasFlowControl = true;
            const char *braceEnd = findClosingBrace(a + 1, argsEnd, '{', '}');
            if (braceEnd < argsEnd) {
                parseSingleMessage(a + 1, braceEnd, ev.flowControlMsg);
                a = stripWhitespace(braceEnd + 1, argsEnd);
                if (*a == ',') a = stripWhitespace(a + 1, argsEnd);
            }
        } else {
            ev.hasFlowControl = false;
        }
        // Filter ID (last arg)
        ev.filterId = parseDecimal(a, (int)(argsEnd - a));
        break;
    }

    case CALL_FILTER_STOP: {
        // PassThruStopMsgFilter(channelId, filterId)
        const char *a = stripWhitespace(argsStart, argsEnd);
        ev.handle = parseDecimal(a, 20);
        const char *comma = findChar(a, argsEnd, ',');
        if (comma < argsEnd) {
            a = stripWhitespace(comma + 1, argsEnd);
            ev.filterId = parseDecimal(a, (int)(argsEnd - a));
        }
        break;
    }

    case CALL_READ_VERSION: {
        // PassThruReadVersion(deviceId, "fw", "dll", "api")
        const char *a = stripWhitespace(argsStart, argsEnd);
        ev.handle = parseDecimal(a, 20);
        const char *comma = findChar(a, argsEnd, ',');
        if (comma < argsEnd) {
            a = stripWhitespace(comma + 1, argsEnd);
            // fw version "3.37.0"
            if (*a == '"') {
                a++;
                const char *qEnd = findChar(a, argsEnd, '"');
                int len = (int)(qEnd - a);
                if (len > 0 && len < (int)sizeof(ev.firmwareVersion)) {
                    memcpy(ev.firmwareVersion, a, len);
                    ev.firmwareVersion[len] = '\0';
                }
                a = stripWhitespace(qEnd + 1, argsEnd);
                if (*a == ',') a = stripWhitespace(a + 1, argsEnd);
            }
            // dll version
            if (*a == '"') {
                a++;
                const char *qEnd = findChar(a, argsEnd, '"');
                int len = (int)(qEnd - a);
                if (len > 0 && len < (int)sizeof(ev.dllVersion)) {
                    memcpy(ev.dllVersion, a, len);
                    ev.dllVersion[len] = '\0';
                }
                a = stripWhitespace(qEnd + 1, argsEnd);
                if (*a == ',') a = stripWhitespace(a + 1, argsEnd);
            }
            // api version
            if (*a == '"') {
                a++;
                const char *qEnd = findChar(a, argsEnd, '"');
                int len = (int)(qEnd - a);
                if (len > 0 && len < (int)sizeof(ev.apiVersion)) {
                    memcpy(ev.apiVersion, a, len);
                    ev.apiVersion[len] = '\0';
                }
            }
        }
        break;
    }

    default:
        return false;
    }

    return true;
}

int LogParser::parseMessages(const char *start, const char *end, ReplayEvent &ev) {
    // Parse comma-separated {msg}, {msg}, ... within [...]
    int count = 0;
    const char *p = start;
    while (p < end && count < 16) {
        p = stripWhitespace(p, end);
        if (p >= end) break;
        if (*p == ',') { p++; continue; }
        if (*p == '{') {
            const char *braceEnd = findClosingBrace(p + 1, end, '{', '}');
            if (braceEnd < end) {
                if (parseSingleMessage(p + 1, braceEnd, ev.msgs[count]))
                    count++;
                p = braceEnd + 1;
            } else {
                break;
            }
        } else {
            p++;
        }
    }
    return count;
}

bool LogParser::parseSingleMessage(const char *start, const char *end, ReplayMsg &m) {
    // Format: "ISO15765; RxStatus: 0; TxFlags: ISO15765_FRAME_PAD; TS: 00:00:00.0080000; LEN: 6; ExtraIndex: 0; Data: 00-00-06-02-10-03"
    // Parse semicolon-separated key:value pairs
    memset(&m, 0, sizeof(m));

    const char *p = start;
    // First field is protocol name (no key prefix)
    const char *semi = findChar(p, end, ';');
    if (semi >= end) return false;
    m.protocolId = lookupProtocol(p, (int)(semi - p));
    p = semi + 1;

    while (p < end) {
        p = stripWhitespace(p, end);
        if (p >= end) break;

        // Find "Key: Value" or "Key; Value" pattern
        // Look for known keys
        int rxMatch = startsWith(p, end, "RxStatus:");
        int txMatch = startsWith(p, end, "TxFlags:");
        int tsMatch = startsWith(p, end, "TS:");
        int lenMatch = startsWith(p, end, "LEN:");
        int eiMatch = startsWith(p, end, "ExtraIndex:");
        int dataMatch = startsWith(p, end, "Data:");

        if (rxMatch) {
            p += rxMatch;
            p = stripWhitespace(p, end);
            m.rxStatus = parseDecimal(p, 20);
        } else if (txMatch) {
            p += txMatch;
            p = stripWhitespace(p, end);
            // TxFlags can be named or numeric
            const char *valEnd = findChar(p, end, ';');
            if (valEnd > end) valEnd = end;
            int valLen = (int)(valEnd - p);
            // Trim trailing whitespace
            while (valLen > 0 && (p[valLen-1] == ' ' || p[valLen-1] == '\t'))
                valLen--;
            unsigned long namedFlag = lookupTxFlag(p, valLen);
            if (namedFlag) {
                m.txFlags = namedFlag;
            } else if (p[0] == '0' && valLen > 1 && (p[1] == 'x' || p[1] == 'X')) {
                m.txFlags = parseHex(p + 2, valLen - 2);
            } else {
                m.txFlags = parseDecimal(p, valLen);
            }
        } else if (tsMatch) {
            p += tsMatch;
            p = stripWhitespace(p, end);
            // Timestamp format: 00:00:00.0080000 or 00:00:00
            // Parse H:M:S.microseconds
            unsigned long hours = parseDecimal(p, 2);
            p += 2; if (*p == ':') p++;
            unsigned long mins = parseDecimal(p, 2);
            p += 2; if (*p == ':') p++;
            unsigned long secs = parseDecimal(p, 2);
            p += 2;
            unsigned long micros = 0;
            if (*p == '.') {
                p++;
                // Parse fractional seconds, take first 6 digits as microseconds
                const char *fracStart = p;
                unsigned long frac = parseDecimal(fracStart, 7);
                int fracLen = 0;
                while (fracStart + fracLen < end && fracStart[fracLen] >= '0' && fracStart[fracLen] <= '9')
                    fracLen++;
                // Normalize to microseconds (7 digits in the log: .0080000 = 8000us)
                while (fracLen < 6) { frac *= 10; fracLen++; }
                while (fracLen > 6 && frac > 0) { frac /= 10; fracLen--; }
                micros = frac;
            }
            m.timestamp = (hours * 3600 + mins * 60 + secs) * 1000000 + micros;
        } else if (lenMatch) {
            p += lenMatch;
            p = stripWhitespace(p, end);
            m.dataSize = parseDecimal(p, 20);
        } else if (eiMatch) {
            p += eiMatch;
            p = stripWhitespace(p, end);
            m.extraDataIndex = parseDecimal(p, 20);
        } else if (dataMatch) {
            p += dataMatch;
            p = stripWhitespace(p, end);
            // Parse hex bytes: AA-BB-CC-...
            unsigned long idx = 0;
            while (p < end && idx < sizeof(m.data)) {
                if (*p == ' ' || *p == '\t') { p++; continue; }
                if (*p == '-' || *p == ';') { p++; continue; }
                if ((p[0] >= '0' && p[0] <= '9') || (p[0] >= 'a' && p[0] <= 'f') || (p[0] >= 'A' && p[0] <= 'F')) {
                    if (p + 1 < end) {
                        unsigned long hi = parseHex(p, 1);
                        unsigned long lo = parseHex(p + 1, 1);
                        m.data[idx++] = (unsigned char)(hi * 16 + lo);
                        p += 2;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            if (m.dataSize == 0) m.dataSize = idx;
            return true;
        } else {
            // Skip unknown field
        }

        // Advance to next semicolon
        semi = findChar(p, end, ';');
        if (semi < end)
            p = semi + 1;
        else
            break;
    }

    return m.dataSize > 0 || m.protocolId != 0;
}
