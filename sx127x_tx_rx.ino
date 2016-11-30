/* SX1272/SX1276 transmit & receive */
/* Thomas Telkamp */

/* --------------------------- */

// RFM92 = SX1272, RFM95 = SX1276
//#define SX1272  1
#define SX1276  1

#define TRANSMIT  1
//#define RECEIVE   1

#define SSPIN   10  // 10 for mjs board
#define DIO0    2   //  2 for mjs board

#define TXDELAY 20000  // in milliseconds

/* --------------------------- */

// Set center frequency
uint32_t  freq = 868100000; // in Mhz! (868.1)

// Set tx payload
char payload[] = "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";

int payloadlength = sizeof(payload);
char message[256];

/* --------------------------- */


#if defined(SX1272) 
  #include "sx1272Regs-LoRa.h"
#elif defined(SX1276) 
  #include "sx1276Regs-LoRa.h"
#endif

#include <SPI.h>

// MODES
#define MODE_RX_CONTINOUS      (RFLR_OPMODE_LONGRANGEMODE_ON | RFLR_OPMODE_RECEIVER) // 0x85
#define MODE_TX                (RFLR_OPMODE_LONGRANGEMODE_ON | RFLR_OPMODE_TRANSMITTER) // 0x83
#define MODE_SLEEP             (RFLR_OPMODE_LONGRANGEMODE_ON | RFLR_OPMODE_SLEEP) // 0x80
#define MODE_STANDBY           (RFLR_OPMODE_LONGRANGEMODE_ON | RFLR_OPMODE_STANDBY) // 0x81

#define FRF_MSB                  0xD9 // 868.1 Mhz (see datasheet)
#define FRF_MID                  0x06 
#define FRF_LSB                  0x66 

#if defined(SX1272) 
  #define LNA_MAX_GAIN                (RFLR_LNA_GAIN_G1 | RFLR_LNA_BOOST_ON)  // 0x23 sx1272
  #define LNA_OFF_GAIN                RFLR_LNA_BOOST_OFF
#elif defined(SX1276) 
  #define LNA_MAX_GAIN                (RFLR_LNA_GAIN_G1 | RFLR_LNA_BOOST_HF_ON)  // 0x23 sx1276
  #define LNA_OFF_GAIN                RFLR_LNA_BOOST_HF_OFF 
#endif


#define PA_MAX_BOOST                0x8F
#define PA_LOW_BOOST                0x81
#define PA_MED_BOOST                0x8A
#define PA_OFF_BOOST                0x00


#if defined(SX1272) 
  // For SX1272/RFM92
  #define MODEMCONFIG1   (RFLR_MODEMCONFIG1_BW_125_KHZ| RFLR_MODEMCONFIG1_RXPAYLOADCRC_ON | RFLR_MODEMCONFIG1_IMPLICITHEADER_OFF | RFLR_MODEMCONFIG1_CODINGRATE_4_5)
  #define MODEMCONFIG2   (RFLR_MODEMCONFIG2_SF_8 | RFLR_MODEMCONFIG2_AGCAUTO_ON)
#elif defined(SX1276) 
  // For SX1276/RFM95
  #define MODEMCONFIG1   (RFLR_MODEMCONFIG1_IMPLICITHEADER_OFF | RFLR_MODEMCONFIG1_CODINGRATE_4_5 | RFLR_MODEMCONFIG1_BW_125_KHZ)
  #define MODEMCONFIG2   (RFLR_MODEMCONFIG2_SF_8 | RFLR_MODEMCONFIG2_TXCONTINUOUSMODE_OFF| RFLR_MODEMCONFIG2_RXPAYLOADCRC_ON)
#endif

void setup() {
  
  // initialize the pins
  pinMode(SSPIN, OUTPUT); 
  pinMode(DIO0,  INPUT);

  while ((!Serial) && (millis() < 2000));
  
  Serial.begin(115200);
  
  SPI.begin();
  
  // LoRa mode 
  writeRegister(REG_LR_OPMODE,MODE_SLEEP);

  // Frequency
  //writeRegister(REG_LR_FRFMSB,FRF_MSB);
  //writeRegister(REG_LR_FRFMID,FRF_MID);
  //writeRegister(REG_LR_FRFLSB,FRF_LSB);
  
  // Frequency
  uint64_t frf = ((uint64_t)freq << 19) / 32000000;
  writeRegister(REG_LR_FRFMSB, (uint8_t)(frf>>16) );
  writeRegister(REG_LR_FRFMID, (uint8_t)(frf>> 8) );
  writeRegister(REG_LR_FRFLSB, (uint8_t)(frf>> 0) );

  // Turn on implicit header mode and set payload length
  writeRegister(REG_LR_MODEMCONFIG1, MODEMCONFIG1);
  writeRegister(REG_LR_MODEMCONFIG2, MODEMCONFIG2);
  writeRegister(REG_LR_PARAMP,RFLR_PARAMP_0050_US);
  writeRegister(REG_LR_PAYLOADLENGTH, payloadlength);
  writeRegister(REG_LR_SYNCWORD,0x34);  // LoRaWAN Public = 0x34, Private = 0x12

#if defined(TRANSMIT) 
  // Change the DIO mapping to 01 so we can listen for TxDone on the interrupt
  writeRegister(REG_LR_DIOMAPPING1, RFLR_DIOMAPPING1_DIO0_01);
#elif defined(RECEIVE)
  // RX
  writeRegister(REG_LR_DIOMAPPING1, RFLR_DIOMAPPING1_DIO0_00);
  writeRegister(REG_LR_DIOMAPPING2, 0x00);
#endif

  // Go to standby mode
  writeRegister(REG_LR_OPMODE,MODE_STANDBY);

#if defined(RECEIVE) 
  startrx();
#endif
  
  Serial.println("Setup Complete");
  
}

void loop() {

#if defined(TRANSMIT) 
  txloop();
#elif defined(RECEIVE)
  rxloop();
#endif

}


void startrx(){
  
  writeRegister(REG_LR_FIFOADDRPTR, readRegister(REG_LR_FIFORXBASEADDR));   
  
  writeRegister(REG_LR_PACONFIG, PA_OFF_BOOST);   // TURN PA OFF FOR RECIEVE?
  writeRegister(REG_LR_LNA, LNA_MAX_GAIN);        // MAX GAIN FOR RECIEVE
  writeRegister(REG_LR_OPMODE, MODE_RX_CONTINOUS);
  
}

void rxloop() {

  if(digitalRead(DIO0) == 1)
  {
     int irqflags = readRegister(REG_LR_IRQFLAGS); // if any of these are set then the inbound message failed
     //Serial.println(irqflags);

     // Todo: Check RXDONE interrupt

     // clear the rxDone flag
     writeRegister(REG_LR_IRQFLAGS, 0x40); 
     
    // check for payload crc issues (0x20 is the bit we are looking for
    if((irqflags & 0x20) == 0x20)
    {
     Serial.println("Oops there was a crc problem!!");
     //Serial.println(x);
     // reset the crc flags
      writeRegister(REG_LR_IRQFLAGS, 0x20); 
    }
  else{
    byte currentAddr = readRegister(REG_LR_FIFORXCURRENTADDR);
    byte receivedCount = readRegister(REG_LR_RXNBBYTES);
    //Serial.print("Packet! RX Current Addr:");
    //Serial.println(currentAddr);
    Serial.print("Number of bytes received: ");
    Serial.println(receivedCount);
    Serial.print("Signal strength (rssi): ");
#if defined(SX1272) 
    Serial.println(readRegister(REG_LR_PKTRSSIVALUE)-125);
#elif defined(SX1276)
    Serial.println(readRegister(REG_LR_PKTRSSIVALUE)-157);
#endif
    Serial.print("Message: ");
    
    writeRegister(REG_LR_FIFOADDRPTR, currentAddr);   
    // now loop over the fifo getting the data
    for(int i = 0; i < receivedCount; i++)
    {
      message[i] = (char)readRegister(REG_LR_FIFO);
      Serial.print(message[i]);
    }
    Serial.println("");
  } 
  }
}


void txloop() {

  // Send
  writeRegister(REG_LR_OPMODE,MODE_STANDBY);
  writeRegister(REG_LR_FIFOTXBASEADDR , 0x00);
  writeRegister(REG_LR_FIFOADDRPTR, 0x00); 
  
  select();

  SPI.transfer(REG_LR_FIFO | 0x80);
  for (int i = 0; i < payloadlength; i++){
    Serial.print(payload[i]);
    Serial.print(" ");
    SPI.transfer(payload[i]);
  }
  Serial.println();
  
  unselect();

  writeRegister(REG_LR_LNA, LNA_OFF_GAIN);  // TURN LNA OFF FOR TRANSMITT
  writeRegister(REG_LR_PACONFIG, PA_MED_BOOST);    // TURN PA TO MAX POWER
  writeRegister(REG_LR_OPMODE, MODE_TX);
  
  Serial.println("Wait for dio0 (txdone)");  
  while(digitalRead(DIO0) == 0)  {  Serial.print(". "); delay(10);  }
  Serial.println("");
  
  // clear the flags 0x08 is the TxDone flag
  writeRegister(REG_LR_IRQFLAGS, RFLR_IRQFLAGS_TXDONE_MASK); 
  
  delay(TXDELAY);
}

byte readRegister(byte addr)
{
  select();
  SPI.transfer(addr & 0x7F);
  byte regval = SPI.transfer(0);
  unselect();
  return regval;
}


void writeRegister(byte addr, byte value)
{
  select();
  SPI.transfer(addr | 0x80); // OR address with 10000000 to indicate write enable;
  SPI.transfer(value);
  unselect();
}

void select() 
{
  digitalWrite(SSPIN, LOW);
}


void unselect() 
{
  digitalWrite(SSPIN, HIGH);
}

