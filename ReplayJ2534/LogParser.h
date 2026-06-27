#pragma once
// ReplayJ2534 — Log file parser
// Parses JSON log entries containing human-readable J2534 API call traces

#include "J2534Defs.h"

struct ReplayMsg {
    unsigned long protocolId;
    unsigned long rxStatus;
    unsigned long txFlags;
    unsigned long timestamp;       // microseconds
    unsigned long extraDataIndex;
    unsigned char data[4128];
    unsigned long dataSize;
};

enum ReplayCallType {
    CALL_NONE = 0,
    CALL_OPEN,
    CALL_CLOSE,
    CALL_CONNECT,
    CALL_DISCONNECT,
    CALL_WRITE,
    CALL_READ,
    CALL_IOCTL,
    CALL_FILTER_START,
    CALL_FILTER_STOP,
    CALL_READ_VERSION,
    CALL_PERIODIC_START,
    CALL_PERIODIC_STOP
};

struct ReplayEvent {
    ReplayCallType type;
    unsigned long handle;          // device or channel ID
    unsigned long returnCode;
    unsigned long count;           // dedup multiplier

    // Open params
    char openName[256];

    // Connect params
    unsigned long protocolId;
    unsigned long connectFlags;
    unsigned long baudRate;
    unsigned long outChannelId;    // channel ID returned by Connect

    // Messages (for Write/Read)
    ReplayMsg msgs[16];
    unsigned long numMsgs;
    unsigned long numMsgsIn;       // requested count
    unsigned long numMsgsOut;      // actual count (for matching)
    unsigned long timeout;

    // Ioctl params
    unsigned long ioctlId;
    bool ioctlIdKnown;
    bool hasInput;
    unsigned char ioctlIn[512];
    unsigned long ioctlInSize;
    bool hasOutput;
    unsigned char ioctlOut[512];
    unsigned long ioctlOutSize;

    // Filter params
    unsigned long filterType;
    ReplayMsg maskMsg;
    ReplayMsg patternMsg;
    ReplayMsg flowControlMsg;
    bool hasFlowControl;
    unsigned long filterId;

    // Periodic msg params
    unsigned long periodicMsgId;
    unsigned long timeInterval;
    ReplayMsg periodicMsg;

    // Version strings (for ReadVersion)
    char firmwareVersion[80];
    char dllVersion[80];
    char apiVersion[80];
};

class LogParser {
public:
    LogParser();
    ~LogParser();

    bool load(const char *filePath);
    void clear();
    int eventCount() const { return eventCount_; }
    const ReplayEvent* getEvent(int index) const;

    // Static string helpers (public so free functions in LogParser.cpp can use them)
    static const char* stripWhitespace(const char *p, const char *end);
    static const char* findChar(const char *p, const char *end, char c);
    static const char* findClosingBrace(const char *p, const char *end, char open, char close);
    static int strEqual(const char *a, int aLen, const char *b);
    static int startsWith(const char *p, const char *end, const char *prefix);
    static unsigned long parseHex(const char *p, int maxLen);
    static unsigned long parseDecimal(const char *p, int maxLen);
    static unsigned long lookupConfigParam(const char *name, int len);

private:
    bool parseJsonFile(const char *filePath);
    bool ensureCapacity(int needed);
    bool parseEntry(const char *jsonObj, int jsonLen);
    bool parseCallText(const char *text, ReplayEvent &ev);
    int  parseMessages(const char *start, const char *end, ReplayEvent &ev);
    bool parseSingleMessage(const char *start, const char *end, ReplayMsg &m);
    unsigned long lookupError(const char *name, int len);
    unsigned long lookupProtocol(const char *name, int len);
    unsigned long lookupIoctl(const char *name, int len);
    unsigned long lookupConnectFlag(const char *name, int len);
    unsigned long lookupTxFlag(const char *name, int len);
    unsigned long lookupFilterType(const char *name, int len);

    ReplayEvent *events_;
    int eventCount_;
    int eventCapacity_;
};
