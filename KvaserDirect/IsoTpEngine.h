#pragma once
// ISO 15765-2 (ISO-TP) Transport Protocol Engine
// Implements segmentation, reassembly, and flow control on raw CAN frames.

#include "J2534Defs.h"
#include "CanlibLoader.h"
#include <windows.h>

// ISO-TP PCI (Protocol Control Information) types — high nibble of first byte
#define ISOTP_PCI_SF  0x00  // Single Frame
#define ISOTP_PCI_FF  0x10  // First Frame
#define ISOTP_PCI_CF  0x20  // Consecutive Frame
#define ISOTP_PCI_FC  0x30  // Flow Control

// Flow Status in FC frames
#define ISOTP_FS_CTS    0   // Continue To Send
#define ISOTP_FS_WAIT   1   // Wait (sender should pause)
#define ISOTP_FS_OVFLW  2   // Overflow (abort)

// Timeouts (ms) per ISO 15765-2
#define ISOTP_N_AS_DEFAULT   1000
#define ISOTP_N_AR_DEFAULT   1000
#define ISOTP_N_BS_DEFAULT   1000
#define ISOTP_N_CR_DEFAULT   1000

// Limits
#define ISOTP_MAX_MSG_LEN    4095   // Max ISO-TP message (12-bit length in FF)
#define ISOTP_MAX_SESSIONS   16     // Per channel
#define ISOTP_RX_QUEUE_SIZE  64     // Completed messages queue depth
#define ISOTP_MAX_WFT        10     // Max WAIT frames before abort

// TX state
enum IsoTpTxState {
    TX_IDLE = 0,
    TX_WAIT_FC,         // FF sent, waiting for FC from receiver
    TX_SENDING_CF,      // Sending CF burst (BS count)
};

// RX state
enum IsoTpRxState {
    RX_IDLE = 0,
    RX_RECEIVING,       // FF received, collecting CFs
};

// A single ISO-TP session (one per flow control filter pair)
struct IsoTpSession {
    bool            active;
    unsigned long   txId;           // CAN ID we send on (our address)
    unsigned long   rxId;           // CAN ID we receive from (ECU response address)
    unsigned int    txFlags;        // canMSG_EXT for 29-bit
    unsigned int    rxFlags;        // canMSG_EXT for 29-bit
    unsigned long   filterId;       // J2534 filter handle that created this session

    // TX state
    IsoTpTxState    txState;
    unsigned char   txBuf[ISOTP_MAX_MSG_LEN];
    unsigned long   txLen;          // Total bytes to send
    unsigned long   txOffset;       // Bytes sent so far
    unsigned char   txSN;           // Sequence Number (0-15, wraps)
    unsigned long   txBS;           // Block Size from FC (0=unlimited)
    unsigned long   txBsSent;       // Frames sent in current block
    unsigned long   txSTmin;        // STmin from FC (ms or µs encoded)
    DWORD           txFcDeadline;   // Tick count deadline for FC timeout

    // RX state
    IsoTpRxState    rxState;
    unsigned char   rxBuf[ISOTP_MAX_MSG_LEN];
    unsigned long   rxLen;          // Expected total length
    unsigned long   rxOffset;       // Bytes received so far
    unsigned char   rxSN;           // Expected next SN
    unsigned long   rxBsCount;      // Frames received in current block
    DWORD           rxCfDeadline;   // Tick count deadline for CF timeout
    unsigned long   rxWftCount;     // Wait frames sent
};

// Completed ISO-TP message (delivered to J2534 app)
struct IsoTpRxMsg {
    unsigned long   canId;          // Source CAN ID
    unsigned int    rxFlags;        // 29-bit etc
    unsigned long   dataLen;        // Payload length (ISO-TP PDU)
    unsigned char   data[ISOTP_MAX_MSG_LEN];
    unsigned long   timestamp;      // Timestamp of first frame
};

// The ISO-TP engine — one instance per ISO15765 channel
class IsoTpEngine {
public:
    IsoTpEngine();
    ~IsoTpEngine();

    // Initialize with CANlib handle and config
    void init(CanHandle hnd, CanlibApi *api,
              unsigned long bs_tx, unsigned long stmin_tx,
              unsigned long pad_value, unsigned long wft_max);
    void shutdown();

    // Session management (tied to FLOW_CONTROL filters)
    int  addSession(unsigned long txId, unsigned long rxId,
                    unsigned int txFlags, unsigned int rxFlags,
                    unsigned long filterId);
    void removeSession(unsigned long filterId);

    // TX: segment and send a complete ISO-TP message
    // Returns J2534 status code. Blocks up to timeout for FC.
    long transmit(unsigned long txId, unsigned int txFlags,
                  const unsigned char *data, unsigned long len,
                  unsigned long timeout);

    // RX: retrieve completed reassembled messages
    // Returns number delivered (up to *count). Sets *count on return.
    long receive(PASSTHRU_MSG *msgs, unsigned long *count, unsigned long timeout);

    // Process a raw CAN frame from the bus (called by RX thread)
    void processFrame(long canId, unsigned int canFlags,
                      const unsigned char *data, unsigned int dlc,
                      unsigned long timestamp);

    // Update config at runtime (SET_CONFIG)
    void setConfig(unsigned long bs_tx, unsigned long stmin_tx,
                   unsigned long pad_value, unsigned long wft_max);

private:
    CanHandle       hnd_;
    CanlibApi      *api_;

    // Config
    unsigned long   bsTx_;          // BS we advertise in our FC
    unsigned long   stminTx_;       // STmin we advertise in our FC (ms)
    unsigned long   padValue_;      // Padding byte (0xCC default)
    unsigned long   wftMax_;        // Max WAIT frames to send

    // Sessions
    IsoTpSession    sessions_[ISOTP_MAX_SESSIONS];
    CRITICAL_SECTION lock_;

    // Completed RX queue
    IsoTpRxMsg      rxQueue_[ISOTP_RX_QUEUE_SIZE];
    int             rxHead_;
    int             rxTail_;
    int             rxCount_;
    HANDLE          rxEvent_;       // Signaled when message available

    // Internal helpers
    IsoTpSession*   findSessionByRxId(long canId, unsigned int flags);
    IsoTpSession*   findSessionByTxId(unsigned long txId, unsigned int flags);
    void            sendFC(IsoTpSession *s, unsigned char flowStatus);
    void            sendCF(IsoTpSession *s);
    void            deliverMessage(IsoTpSession *s, unsigned long timestamp);
    void            padFrame(unsigned char *frame, int usedBytes, int totalBytes);
    unsigned long   decodeSTmin(unsigned char raw);
    unsigned char   encodeSTmin(unsigned long ms);
    void            enqueueRx(const IsoTpRxMsg &msg);
};
