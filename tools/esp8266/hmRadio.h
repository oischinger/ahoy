//-----------------------------------------------------------------------------
// 2022 Ahoy, https://www.mikrocontroller.net/topic/525778
// Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//-----------------------------------------------------------------------------

#ifndef __RADIO_H__
#define __RADIO_H__

#include "dbg.h"
#include <RF24.h>
#include <RF24_config.h>
#include "crc.h"

//#define CHANNEL_HOP // switch between channels or use static channel to send

#define DEFAULT_RECV_CHANNEL    3
#define SPI_SPEED               1000000

#define DTU_RADIO_ID            ((uint64_t)0x1234567801ULL)
#define DUMMY_RADIO_ID          ((uint64_t)0xDEADBEEF01ULL)

#define RX_CHANNELS             5
#define RX_LOOP_CNT             300


const char* const rf24AmpPower[] = {"MIN", "LOW", "HIGH", "MAX"};


//-----------------------------------------------------------------------------
// MACROS
//-----------------------------------------------------------------------------
#define CP_U32_LittleEndian(buf, v) ({ \
    uint8_t *b = buf; \
    b[0] = ((v >> 24) & 0xff); \
    b[1] = ((v >> 16) & 0xff); \
    b[2] = ((v >>  8) & 0xff); \
    b[3] = ((v      ) & 0xff); \
})

#define CP_U32_BigEndian(buf, v) ({ \
    uint8_t *b = buf; \
    b[3] = ((v >> 24) & 0xff); \
    b[2] = ((v >> 16) & 0xff); \
    b[1] = ((v >>  8) & 0xff); \
    b[0] = ((v      ) & 0xff); \
})

#define BIT_CNT(x)  ((x)<<3)


//-----------------------------------------------------------------------------
// HM Radio class
//-----------------------------------------------------------------------------
template <uint8_t CE_PIN, uint8_t CS_PIN, uint8_t IRQ_PIN, class BUFFER, uint64_t DTU_ID=DTU_RADIO_ID>
class HmRadio {
    public:
        HmRadio() : mNrf24(CE_PIN, CS_PIN, SPI_SPEED) {
            DPRINT(DBG_VERBOSE, F("hmRadio.h : HmRadio():mNrf24(CE_PIN: "));
            DPRINT(DBG_VERBOSE, String(CE_PIN));
            DPRINT(DBG_VERBOSE,F(", CS_PIN: "));
            DPRINT(DBG_VERBOSE, String(CS_PIN));
            DPRINT(DBG_VERBOSE, F(", SPI_SPEED: "));
            DPRINTLN(DBG_VERBOSE, String(SPI_SPEED) + ")");
            mTxChLst[0] = 40;
            //mTxChIdx = 1;

            // Depending on the program, the module can work on 2403, 2423, 2440, 2461 or 2475MHz.
            // Channel List      2403, 2423, 2440, 2461, 2475MHz
            mRxChLst[0] = 03;
            mRxChLst[1] = 23;
            mRxChLst[2] = 40;
            mRxChLst[3] = 61;
            mRxChLst[4] = 75;
            mRxChIdx    = 0;
            mRxLoopCnt  = RX_LOOP_CNT;

            pinCs  = CS_PIN;
            pinCe  = CE_PIN;
            pinIrq = IRQ_PIN;

            AmplifierPower = 1;
            mSendCnt       = 0;

            mSerialDebug = false;
            mIrqRcvd     = false;
        }
        ~HmRadio() {}

        void setup(BUFFER *ctrl) {
            DPRINTLN(DBG_VERBOSE, F("hmRadio.h:setup"));
            pinMode(pinIrq, INPUT_PULLUP);

            mBufCtrl = ctrl;

            mNrf24.begin(pinCe, pinCs);
            mNrf24.setRetries(0, 0);

            mNrf24.setChannel(DEFAULT_RECV_CHANNEL);
            mNrf24.setDataRate(RF24_250KBPS);
            mNrf24.setCRCLength(RF24_CRC_16);
            mNrf24.setAutoAck(false);
            mNrf24.setPayloadSize(MAX_RF_PAYLOAD_SIZE);
            mNrf24.setAddressWidth(5);
            mNrf24.openReadingPipe(1, DTU_RADIO_ID);
            mNrf24.enableDynamicPayloads();

            // enable only receiving interrupts
            mNrf24.maskIRQ(true, true, false);

            DPRINT(DBG_INFO, F("RF24 Amp Pwr: RF24_PA_"));
            DPRINTLN(DBG_INFO, String(rf24AmpPower[AmplifierPower]));
            mNrf24.setPALevel(AmplifierPower & 0x03);
            mNrf24.startListening();

            DPRINTLN(DBG_INFO, F("Radio Config:"));
            mNrf24.printPrettyDetails();

            mTxCh = getDefaultChannel();

            if(!mNrf24.isChipConnected()) {
                DPRINTLN(DBG_WARN, F("WARNING! your NRF24 module can't be reached, check the wiring"));
            }
        }

        void loop(void) {
            DISABLE_IRQ;
            if(mIrqRcvd) {
                mIrqRcvd = false;
                bool tx_ok, tx_fail, rx_ready;
                mNrf24.whatHappened(tx_ok, tx_fail, rx_ready); // resets the IRQ pin to HIGH
                RESTORE_IRQ;
                uint8_t pipe, len;
                packet_t *p;
                while(mNrf24.available(&pipe)) {
                    if(!mBufCtrl->full()) {
                        p = mBufCtrl->getFront();
                        p->rxCh = mRxChLst[mRxChIdx];
                        len = mNrf24.getPayloadSize();
                        if(len > MAX_RF_PAYLOAD_SIZE)
                            len = MAX_RF_PAYLOAD_SIZE;

                        mNrf24.read(p->packet, len);
                        mBufCtrl->pushFront(p);
                    }
                    else {
                        mNrf24.flush_rx(); // drop the packet
                        break;
                    }
                    yield();
                }
            }
            else
                RESTORE_IRQ;
        }

        void handleIntr(void) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:handleIntr"));
            mIrqRcvd = true;
        }

        uint8_t getDefaultChannel(void) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:getDefaultChannel"));
            return mTxChLst[0];
        }
        /*uint8_t getLastChannel(void) {
            return mTxChLst[mTxChIdx];
        }

        uint8_t getNxtChannel(void) {
            if(++mTxChIdx >= 4)
                mTxChIdx = 0;
            return mTxChLst[mTxChIdx];
        }*/

        void sendTimePacket(uint64_t invId, uint32_t ts) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:sendTimePacket"));
            sendCmdPacket(invId, 0x15, 0x80, false);
            mTxBuf[10] = 0x0b; // cid
            mTxBuf[11] = 0x00;
            CP_U32_LittleEndian(&mTxBuf[12], ts);
            mTxBuf[19] = 0x05;

            uint16_t crc = Hoymiles::crc16(&mTxBuf[10], 14);
            mTxBuf[24] = (crc >> 8) & 0xff;
            mTxBuf[25] = (crc     ) & 0xff;
            mTxBuf[26] = Hoymiles::crc8(mTxBuf, 26);

            sendPacket(invId, mTxBuf, 27, true);
        }

        void sendCmdPacket(uint64_t invId, uint8_t mid, uint8_t pid, bool calcCrc = true) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:sendCmdPacket"));
            memset(mTxBuf, 0, MAX_RF_PAYLOAD_SIZE);
            mTxBuf[0] = mid; // message id
            CP_U32_BigEndian(&mTxBuf[1], (invId  >> 8));
            CP_U32_BigEndian(&mTxBuf[5], (DTU_ID >> 8));
            mTxBuf[9]  = pid;
            if(calcCrc) {
                mTxBuf[10] = Hoymiles::crc8(mTxBuf, 10);
                sendPacket(invId, mTxBuf, 11, false);
            }
        }

        void sendTurnOnOffPacket(uint64_t invId, bool onoff, bool calcCrc = true) {
            memset(mTxBuf, 0, MAX_RF_PAYLOAD_SIZE);
            mTxBuf[0] = 0x51; // message id
            CP_U32_BigEndian(&mTxBuf[1], (invId  >> 8));
            CP_U32_BigEndian(&mTxBuf[5], (DTU_ID >> 8));
            mTxBuf[9]  = 0x81;
            mTxBuf[10] = onoff ? 0x00 : 0x01;
            mTxBuf[11] = 0x00; 
            
            if(calcCrc) {
                uint16_t crc = Hoymiles::crc16(&mTxBuf[10], 2);
                mTxBuf[12] = (crc >> 8) & 0xff;
                mTxBuf[13] = (crc     ) & 0xff;

                mTxBuf[14] = Hoymiles::crc8(mTxBuf, 14);
                sendPacket(invId, mTxBuf, 15, false);
                DPRINT(DBG_INFO, "Transmit (TurnOnOff) " + String(15) + " | ");
                dumpBuf(NULL, mTxBuf, 15);
            }
        }

        void sendRestartPacket(uint64_t invId, bool calcCrc = true) {
            memset(mTxBuf, 0, MAX_RF_PAYLOAD_SIZE);
            mTxBuf[0] = 0x51; // message id
            CP_U32_BigEndian(&mTxBuf[1], (invId  >> 8));
            CP_U32_BigEndian(&mTxBuf[5], (DTU_ID >> 8));
            mTxBuf[9]  = 0x81;
            mTxBuf[10] = 0x02;
            mTxBuf[11] = 0x00; 
            
            if(calcCrc) {
                uint16_t crc = Hoymiles::crc16(&mTxBuf[10], 2);
                mTxBuf[12] = (crc >> 8) & 0xff;
                mTxBuf[13] = (crc     ) & 0xff;

                mTxBuf[14] = Hoymiles::crc8(mTxBuf, 14);
                sendPacket(invId, mTxBuf, 15, false);
                DPRINT(DBG_INFO, "Transmit (Restart) " + String(15) + " | ");
                dumpBuf(NULL, mTxBuf, 15);
            }
        }

        void sendPowerLimitPacket(uint64_t invId, uint8_t limit, bool calcCrc = true) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:sendCmdPacket"));
            memset(mTxBuf, 0, MAX_RF_PAYLOAD_SIZE);
            mTxBuf[0] = 0x51; // message id
            CP_U32_BigEndian(&mTxBuf[1], (invId  >> 8));
            CP_U32_BigEndian(&mTxBuf[5], (DTU_ID >> 8));
            mTxBuf[9]  = 0x81;
            mTxBuf[10] = 0x0B; 
            mTxBuf[11] = 0x00;
            
            // mTxBuf[12] = 0x01; // 30,0W 2 bits
            // mTxBuf[13] = 0xF4;
            mTxBuf[12] = (limit >> 8) & 0xff;
            mTxBuf[13] = (limit     ) & 0xff;

            mTxBuf[14] = 0x01; 
            mTxBuf[15] = 0x00; 

            // 51 72 61 55 82 78 56 34 12 81 0B 00 01 2C 01 00 C5 C0 3E 
            //    51 72 61 55 82 78 56 34 12 81 0B 00 01 2C 01 00 C5 C0 3E 
            // mTxBuf[1] = 0x72; 
            // mTxBuf[2] = 0x61; 
            // mTxBuf[3] = 0x55; 
            // mTxBuf[4] = 0x82; 
            // mTxBuf[5] = 0x78; 
            // mTxBuf[6] = 0x56; 
            // mTxBuf[7] = 0x34; 
            // mTxBuf[8] = 0x12; 
            // mTxBuf[9] = 0x81; 
            // mTxBuf[10] = 0x0B; 
            // mTxBuf[11] = 0x00; 
            // mTxBuf[12] = 0x01; 
            // mTxBuf[13] = 0x2C; 
            // mTxBuf[14] = 0x01; 
            // mTxBuf[15] = 0x00; 


// [18:55:14][I][hoymiles.esp32:229]: Sending Limitation: 10
// [18:55:14]I: Transmit (Limit) 19 | 51 73 10 90 25 78 56 34 12 81 0B 00 01 F4 01 00 FE 40 4F 
// [18:55:15][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:55:15]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [18:55:15][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:55:15]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [18:55:15][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:55:15]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [18:55:16][I][hoymiles.esp32:324]: Payload (4): 
// [18:55:16]00 00 0B 00 
// [18:55:35][D][switch:013]: 'HM400 - Switch On/Off' Turning ON.
// [18:55:35][I][hoymiles.esp32:229]: Sending Limitation: 10
// [18:55:35]I: Transmit (Limit) 19 | 51 73 10 90 25 78 56 34 12 81 0B 00 01 F4 01 00 FE 40 4F 
// [18:55:36][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:55:36]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [18:55:36][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:55:36]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [18:55:36][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:55:36]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48    


// [18:59:20][I][hoymiles.esp32:229]: Sending Limitation: 10
// [18:59:20]I: Transmit (Limit) 19 | 51 73 10 90 25 78 56 34 12 81 0B 00 01 F4 01 00 FE 40 4F 
// [18:59:21][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:59:21]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [18:59:21][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:59:21]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [18:59:21][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [18:59:21]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 

// [19:10:03][I][hoymiles.esp32:229]: Sending Limitation: 10
// [19:10:03]I: Transmit (Limit) 19 | 51 73 10 90 25 78 56 34 12 81 0B 00 00 00 00 20 B8 00 9D 
// [19:10:04][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [19:10:04]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:10:04][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [19:10:04]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:10:04][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [19:10:04]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:10:07][I][hoymiles.esp32:324]: Payload (4): 
// [19:10:07]00 00 0B 00 


// [19:16:30][I][hoymiles.esp32:229]: Sending Limitation: 10
// [19:16:30]I: Transmit (Limit) 19 | 51 73 10 90 25 78 56 34 12 81 0B 00 00 00 01 F4 77 01 86 
// [19:16:31][D][hoymiles.esp32:127]: Received 17 bytes channel 3: 
// [19:16:31]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:16:31][I][hoymiles.esp32:324]: Payload (4): 
// [19:16:31]00 00 0B 00 
// [19:16:31][D][hoymiles.esp32:127]: Received 17 bytes channel 3: 
// [19:16:31]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:16:31][D][hoymiles.esp32:127]: Received 17 bytes channel 3: 
// [19:16:31]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:16:31][D][hoymiles.esp32:127]: Received 17 bytes channel 3: 
// [19:16:31]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:16:31][D][hoymiles.esp32:127]: Received 17 bytes channel 3: 
// [19:16:31]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:16:31][D][hoymiles.esp32:127]: Received 17 bytes channel 3: 
// [19:16:31]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 


// [19:30:08][I][hoymiles.esp32:229]: Sending Limitation: 10
// [19:30:08]I: Transmit (Limit) 19 | 51 73 10 90 25 78 56 34 12 81 0B 00 01 00 01 F4 8B 00 7A 
// [19:30:09][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [19:30:09]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:30:09][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [19:30:09]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:30:09][D][hoymiles.esp32:127]: Received 17 bytes channel 75: 
// [19:30:09]D1 73 10 90 25 73 10 90 25 81 00 00 0B 00 14 07 48 
// [19:30:11][I][hoymiles.esp32:324]: Payload (4): 
// [19:30:11]00 00 0B 00 
 

            if(calcCrc) {
                uint16_t crc = Hoymiles::crc16(&mTxBuf[10], 6);
                mTxBuf[16] = (crc >> 8) & 0xff;
                mTxBuf[17] = (crc     ) & 0xff;

                mTxBuf[18] = Hoymiles::crc8(mTxBuf, 18);
                sendPacket(invId, mTxBuf, 19, false);
                DPRINT(DBG_INFO, "Transmit (Limit) " + String(19) + " | ");
                dumpBuf(NULL, mTxBuf, 19);
            }
        }

        bool checkPaketCrc(uint8_t buf[], uint8_t *len, uint8_t rxCh) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:checkPaketCrc"));
            *len = (buf[0] >> 2);
            if(*len > (MAX_RF_PAYLOAD_SIZE - 2))
                *len = MAX_RF_PAYLOAD_SIZE - 2;
            for(uint8_t i = 1; i < (*len + 1); i++) {
                buf[i-1] = (buf[i] << 1) | (buf[i+1] >> 7);
            }

            uint8_t crc = Hoymiles::crc8(buf, *len-1);
            bool valid  = (crc == buf[*len-1]);

            return valid;
        }

        bool switchRxCh(uint16_t addLoop = 0) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:switchRxCh"));
            //DPRINTLN(DBG_VERBOSE, F("R"));

            mRxLoopCnt += addLoop;
            if(mRxLoopCnt != 0) {
                mRxLoopCnt--;
                DISABLE_IRQ;
                mNrf24.stopListening();
                mNrf24.setChannel(getRxNxtChannel());
                mNrf24.startListening();
                RESTORE_IRQ;
            }
            return (0 == mRxLoopCnt); // receive finished
        }

        void dumpBuf(const char *info, uint8_t buf[], uint8_t len) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:dumpBuf"));
            if(NULL != info)
                DBGPRINT(String(info));
            for(uint8_t i = 0; i < len; i++) {
                DHEX(buf[i]);
                DBGPRINT(" ");
            }
            DBGPRINTLN("");
        }

        bool isChipConnected(void) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:isChipConnected"));
            return mNrf24.isChipConnected();
        }

        uint8_t pinCs;
        uint8_t pinCe;
        uint8_t pinIrq;

        uint8_t AmplifierPower;
        uint32_t mSendCnt;

        bool mSerialDebug;

    private:
        void sendPacket(uint64_t invId, uint8_t buf[], uint8_t len, bool clear=false, uint8_t channel = NULL) {
            //DPRINTLN(DBG_VERBOSE, F("hmRadio.h:sendPacket"));
            //DPRINTLN(DBG_VERBOSE, "sent packet: #" + String(mSendCnt));
            //dumpBuf("SEN ", buf, len);
            if(mSerialDebug) {
                DPRINT(DBG_INFO, "Transmit " + String(len) + " | ");
                dumpBuf(NULL, buf, len);
            }

            DISABLE_IRQ;
            mNrf24.stopListening();

            if(clear)
                mRxLoopCnt = RX_LOOP_CNT;

            if (!channel) 
                mTxCh = getDefaultChannel();
            
            mNrf24.setChannel(mTxCh);
            mNrf24.openWritingPipe(invId); // TODO: deprecated
            mNrf24.setCRCLength(RF24_CRC_16);
            mNrf24.enableDynamicPayloads();
            mNrf24.setAutoAck(true);
            mNrf24.setRetries(3, 15); // 3*250us and 15 loops -> 11.25ms
            mNrf24.write(buf, len);

            // Try to avoid zero payload acks (has no effect)
            mNrf24.openWritingPipe(DUMMY_RADIO_ID); // TODO: why dummy radio id?, deprecated
            mRxChIdx = 0;
            mNrf24.setChannel(mRxChLst[mRxChIdx]);
            mNrf24.setAutoAck(false);
            mNrf24.setRetries(0, 0);
            mNrf24.disableDynamicPayloads();
            mNrf24.setCRCLength(RF24_CRC_DISABLED);
            mNrf24.startListening();

            RESTORE_IRQ;
            mSendCnt++;
        }

        uint8_t getRxNxtChannel(void) {

            if(++mRxChIdx >= RX_CHANNELS)
                mRxChIdx = 0;
            return mRxChLst[mRxChIdx];
        }

        uint8_t mTxCh;
        uint8_t mTxChLst[1];
        //uint8_t mTxChIdx;

        uint8_t mRxChLst[RX_CHANNELS];

        uint8_t mRxChIdx;
        uint16_t mRxLoopCnt;

        RF24 mNrf24;
        BUFFER *mBufCtrl;
        uint8_t mTxBuf[MAX_RF_PAYLOAD_SIZE];

        volatile bool mIrqRcvd;
};

#endif /*__RADIO_H__*/
