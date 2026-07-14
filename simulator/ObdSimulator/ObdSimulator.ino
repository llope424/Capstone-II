/*
 * ObdSimulator - a "virtual car" OBD-II ECU emulator for bench-testing the
 * OBD Suite scanner app (or SavvyCAN) without a real vehicle.
 *
 * Hardware: Arduino Uno (or Nano) + MCP2515 CAN module.
 * Library:  coryjfowler "MCP_CAN" (install "mcp_can" from the Library Manager).
 *
 * It behaves like a single ECU sitting at the standard OBD-II addresses:
 *   - Listens for requests on 0x7DF (functional broadcast) and 0x7E0 (physical).
 *   - Answers on 0x7E8.
 *   - Mode 01 (current data): returns time-varying sensor values.
 *   - Mode 03 (stored DTCs): returns two example trouble codes.
 *   - Mode 09 PID 02 (VIN): multi-frame ISO-TP response.
 *
 * IMPORTANT: set MCP_CRYSTAL below to match YOUR module's crystal (8 or 16 MHz).
 */

#include <SPI.h>
#include <mcp_can.h>
#include <math.h>

// ---- Configuration ----
const uint8_t CS_PIN = 10;
#define MCP_CRYSTAL MCP_8MHZ   // change to MCP_16MHZ if your module has a 16 MHz crystal
static const char VIN[] = "1HGBH41JXMN109186"; // 17-char sample VIN

const uint32_t ID_FUNCTIONAL = 0x7DF;
const uint32_t ID_PHYSICAL   = 0x7E0;
const uint32_t ID_RESPONSE   = 0x7E8;

MCP_CAN CAN0(CS_PIN);

void setup() {
  Serial.begin(115200);
  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_CRYSTAL) == CAN_OK) {
    Serial.println(F("MCP2515 init OK (500 kbps)"));
  } else {
    Serial.println(F("MCP2515 init FAILED - check wiring and MCP_CRYSTAL setting"));
  }
  CAN0.setMode(MCP_NORMAL);
}

void sendSingleFrame(const uint8_t *payload, uint8_t payloadLen) {
  uint8_t frame[8];
  frame[0] = payloadLen;
  for (uint8_t i = 0; i < 7; i++)
    frame[1 + i] = (i < payloadLen) ? payload[i] : 0x00;
  CAN0.sendMsgBuf(ID_RESPONSE, 0, 8, frame);
}

void handleMode01(uint8_t pid) {
  float t = millis() / 1000.0f;
  uint8_t p[6];
  uint8_t n = 0;
  p[0] = 0x41;
  p[1] = pid;
  switch (pid) {
    case 0x00: p[2] = 0x1C; p[3] = 0x5A; p[4] = 0x90; p[5] = 0x00; n = 6; break;
    case 0x04: p[2] = (uint8_t)(128 + 100 * sin(t * 0.7)); n = 3; break;
    case 0x05: { int temp = 40 + (int)(50 * (1 - exp(-t / 20.0))); p[2] = (uint8_t)(temp + 40); n = 3; break; }
    case 0x06: p[2] = (uint8_t)(128 + 10 * sin(t)); n = 3; break;
    case 0x0A: p[2] = 100; n = 3; break;
    case 0x0C: { uint16_t rpm = (uint16_t)(1500 + 700 * sin(t * 1.5)); uint16_t raw = rpm * 4; p[2] = raw >> 8; p[3] = raw & 0xFF; n = 4; break; }
    case 0x0D: p[2] = (uint8_t)(60 + 55 * sin(t * 0.5)); n = 3; break;
    case 0x0F: p[2] = (uint8_t)(30 + 40); n = 3; break;
    case 0x11: p[2] = (uint8_t)(128 + 120 * sin(t * 2.0)); n = 3; break;
    case 0x14: p[2] = (uint8_t)(90 + 80 * sin(t * 3.0)); p[3] = 0xFF; n = 4; break;
    case 0x42: { uint16_t mv = 13800; p[2] = mv >> 8; p[3] = mv & 0xFF; n = 4; break; }
    default: return;
  }
  sendSingleFrame(p, n);
}

void handleMode03() {
  uint8_t p[6] = {0x43, 0x02, 0x03, 0x01, 0x04, 0x20};
  sendSingleFrame(p, 6);
}

void handleVin() {
  uint8_t payload[20];
  payload[0] = 0x49; payload[1] = 0x02; payload[2] = 0x01;
  for (uint8_t i = 0; i < 17; i++) payload[3 + i] = (uint8_t)VIN[i];
  const uint8_t total = 20;

  uint8_t ff[8];
  ff[0] = 0x10 | ((total >> 8) & 0x0F);
  ff[1] = total & 0xFF;
  for (uint8_t i = 0; i < 6; i++) ff[2 + i] = payload[i];
  CAN0.sendMsgBuf(ID_RESPONSE, 0, 8, ff);

  unsigned long start = millis();
  while (millis() - start < 100) {
    if (CAN0.checkReceive() == CAN_MSGAVAIL) {
      unsigned long rxId; uint8_t len; uint8_t buf[8];
      CAN0.readMsgBuf(&rxId, &len, buf);
      if ((rxId == ID_PHYSICAL || rxId == ID_FUNCTIONAL) && (buf[0] & 0xF0) == 0x30) break;
    }
  }

  uint8_t idx = 6, seq = 1;
  while (idx < total) {
    uint8_t cf[8];
    cf[0] = 0x20 | (seq & 0x0F);
    for (uint8_t i = 0; i < 7; i++) cf[1 + i] = (idx < total) ? payload[idx++] : 0x00;
    CAN0.sendMsgBuf(ID_RESPONSE, 0, 8, cf);
    seq++;
    delay(5);
  }
}

void loop() {
  // Diagnostic heartbeat every 500 ms.
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 500) {
    lastBeat = millis();
    uint8_t beat[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x00, 0x00, 0x00};
    byte ok = CAN0.sendMsgBuf(0x555, 0, 8, beat);
    Serial.print(F("TX heartbeat 0x555 result="));
    Serial.println(ok);
  }

  if (CAN0.checkReceive() != CAN_MSGAVAIL) return;

  unsigned long rxId; uint8_t len; uint8_t buf[8];
  CAN0.readMsgBuf(&rxId, &len, buf);

  Serial.print(F("RX id=0x")); Serial.print(rxId, HEX);
  Serial.print(F(" len=")); Serial.print(len);
  Serial.print(F(" data="));
  for (uint8_t i = 0; i < len; i++) { Serial.print(buf[i], HEX); Serial.print(' '); }
  Serial.println();

  if (rxId != ID_FUNCTIONAL && rxId != ID_PHYSICAL) return;
  if (len < 2) return;

  const uint8_t mode = buf[1];
  const uint8_t pid = buf[2];
  Serial.print(F("  -> request mode 0x")); Serial.print(mode, HEX);
  Serial.print(F(" pid 0x")); Serial.println(pid, HEX);

  switch (mode) {
    case 0x01: handleMode01(pid); break;
    case 0x03: handleMode03(); break;
    case 0x09: if (pid == 0x02) handleVin(); break;
    default: break;
  }
}
