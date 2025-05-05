#include <BLEDevice.h>
#include <BLE2902.h>

#include <SPIFFS.h>

#define pin_dex_rx         32 //16
#define pin_dex_tx         13 //17

#define pin_led_r     25
#define pin_led_g     26
#define pin_led_b     33

#define TELEMETRY_FILE "/telemetry.txt"

boolean toReadtelemetryDEX, toReadtelemetryDDCMP;

BLECharacteristic *mCharacteristic;

void readTelemetryDdcmp() {

  Serial1.begin( 2400, SERIAL_8N1, pin_dex_rx, pin_dex_tx, true );
  Serial1.setTimeout( 3000 );

  //-------------------------------------------------------
  byte buffer_rx[1024];
  byte seq_rr_ddcmp;
  byte seq_xx_ddcmp = 0;
  unsigned int n_bytes_message;
  unsigned int crc;
  byte last_package;

  crc = 0;
  // start...
  Serial1.write(calc_crc_16( &crc, 0x05 ));
  Serial1.write(calc_crc_16( &crc, 0x06));
  Serial1.write(calc_crc_16( &crc, 0x40));
  Serial1.write(calc_crc_16( &crc, 0x00));
  Serial1.write(calc_crc_16( &crc, 0x00)); // mbd
  Serial1.write(calc_crc_16( &crc, 0x01)); // sadd
  Serial1.write( crc % 256 );
  Serial1.write( crc / 256 );

  if (Serial1.readBytes (buffer_rx, 8) != 8 ) {
    return;
  }
  if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x07)) {
    return;
  } // ...stack

  crc = 0;
  // data message header...
  Serial1.write(calc_crc_16( &crc, 0x81));
  Serial1.write(calc_crc_16( &crc, 0x10));            // nn
  Serial1.write(calc_crc_16( &crc, 0x40));            // mm
  Serial1.write(calc_crc_16( &crc, 0x00));            // rr
  Serial1.write(calc_crc_16( &crc, ++seq_xx_ddcmp));  // xx
  Serial1.write(calc_crc_16( &crc, 0x01));            // sadd
  Serial1.write( crc % 256 );
  Serial1.write( crc / 256 );

  crc = 0;
  // who are you...
  Serial1.write(calc_crc_16( &crc, 0x77));
  Serial1.write(calc_crc_16( &crc, 0xe0));
  Serial1.write(calc_crc_16( &crc, 0x00));

  Serial1.write(calc_crc_16( &crc, 0x00)); // security code
  Serial1.write(calc_crc_16( &crc, 0x00));

  Serial1.write(calc_crc_16( &crc, 0x00)); // pass code
  Serial1.write(calc_crc_16( &crc, 0x00));

  Serial1.write(calc_crc_16( &crc, 0x01)); // date dd mm yy
  Serial1.write(calc_crc_16( &crc, 0x01));
  Serial1.write(calc_crc_16( &crc, 0x70));
  Serial1.write(calc_crc_16( &crc, 0x00)); // time hh mm ss
  Serial1.write(calc_crc_16( &crc, 0x00));
  Serial1.write(calc_crc_16( &crc, 0x00));
  Serial1.write(calc_crc_16( &crc, 0x00)); // u2
  Serial1.write(calc_crc_16( &crc, 0x00)); // u1
  Serial1.write(calc_crc_16( &crc, 0x0c)); // 0b-Maintenance 0c-Route Person
  Serial1.write( crc % 256 );
  Serial1.write( crc / 256 );

  if (Serial1.readBytes (buffer_rx, 8) != 8 ) {
    return;
  }
  if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
    return;
  } // ...ack

  if (Serial1.readBytes (buffer_rx, 8) != 8 ) {
    return;
  }
  if (buffer_rx[0] != 0x81) {
    return;
  } // ...data message header

  seq_rr_ddcmp = buffer_rx[4];

  n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
  n_bytes_message += 2; // crc16
  if ( Serial1.readBytes( buffer_rx, n_bytes_message ) != n_bytes_message ) {
    return;
  }
  //  if (buffer_rx[2] != 0x01) {
  //    return;
  //  }
  // ...not rejected

  crc = 0;
  // ack...
  Serial1.write(calc_crc_16(&crc, 0x05));
  Serial1.write(calc_crc_16(&crc, 0x01));
  Serial1.write(calc_crc_16(&crc, 0x40));
  Serial1.write(calc_crc_16(&crc, seq_rr_ddcmp)); // rr
  Serial1.write(calc_crc_16(&crc, 0x00));
  Serial1.write(calc_crc_16(&crc, 0x01));         // sadd
  Serial1.write( crc % 256 );
  Serial1.write( crc / 256 ); // Transmitiu ACK (05 01 40 01 00 01 B8 55)

  crc = 0;
  // data message header...
  Serial1.write(calc_crc_16(&crc, 0x81));
  Serial1.write(calc_crc_16(&crc, 0x09));                   // nm
  Serial1.write(calc_crc_16(&crc, 0x40));                   // mm
  Serial1.write(calc_crc_16(&crc, seq_rr_ddcmp));           // rr
  Serial1.write(calc_crc_16(&crc, ++seq_xx_ddcmp));         // xx
  Serial1.write(calc_crc_16(&crc, 0x01));                   // sadd
  Serial1.write( crc % 256 );
  Serial1.write( crc / 256 );    // Transmitiu DATA_HEADER (81 09 40 01 02 01 46 B0)

  crc = 0;
  Serial1.write(calc_crc_16(&crc, 0x77));
  Serial1.write(calc_crc_16(&crc, 0xE2));
  Serial1.write(calc_crc_16(&crc, 0x00));
  Serial1.write(calc_crc_16(&crc, 0x01)); // audit collection list
  Serial1.write(calc_crc_16(&crc, 0x01));
  Serial1.write(calc_crc_16(&crc, 0x00));
  Serial1.write(calc_crc_16(&crc, 0x00));
  Serial1.write(calc_crc_16(&crc, 0x00));
  Serial1.write(calc_crc_16(&crc, 0x00));
  Serial1.write( crc % 256 );
  Serial1.write( crc / 256 );   // Transmitiu READ_DATA/Audit Collection List (77 E2 00 01 01 00 00 00 00 F0 72)

  if (Serial1.readBytes (buffer_rx, 8) != 8 ) {
    return;
  }
  if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
    return;
  } // ...ack

  if (Serial1.readBytes (buffer_rx, 8) != 8 ) {
    return;
  }
  if (buffer_rx[0] != 0x81) {
    return;
  } // DATA HEADER

  seq_rr_ddcmp = buffer_rx[4];

  n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
  n_bytes_message += 2; // crc16
  if (Serial1.readBytes (buffer_rx, n_bytes_message) != n_bytes_message ) {
    return;
  }
  if (buffer_rx[2] != 0x01) {
    return;
  }

  crc = 0;
  Serial1.write(calc_crc_16(&crc, 0x05));
  Serial1.write(calc_crc_16(&crc, 0x01));
  Serial1.write(calc_crc_16(&crc, 0x40));
  Serial1.write(calc_crc_16(&crc, seq_rr_ddcmp));
  Serial1.write(calc_crc_16(&crc, 0x00));
  Serial1.write(calc_crc_16(&crc, 0x01));
  Serial1.write(crc % 256);
  Serial1.write(crc / 256); // Transmitiu ACK

  File fileEva_dts = SPIFFS.open(TELEMETRY_FILE, FILE_WRITE);

  do {
    if (Serial1.readBytes(buffer_rx, 8) != 8 ) {
      break;
    }
    if (buffer_rx[0] != 0x81) {
      break;
    } // ...data header

    seq_rr_ddcmp = buffer_rx[4];
    last_package = buffer_rx[2] & 0x80;

    n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
    n_bytes_message += 2; // crc16

    if (Serial1.readBytes (buffer_rx, n_bytes_message) != n_bytes_message ) {
      break;
    } // dados

    // Os dados recebidos são: 99 nn "audit dada" crc crc, ou seja, as informaões de audit estão da posição 2 do buffer_rx à posição n_bytes-3
    for ( int x = 2; x < n_bytes_message - 2; x++ ) fileEva_dts.print( (char) buffer_rx[ x ] );

    crc = 0;
    Serial1.write(calc_crc_16(&crc, 0x05));
    Serial1.write(calc_crc_16(&crc, 0x01));
    Serial1.write(calc_crc_16(&crc, 0x40));
    Serial1.write(calc_crc_16(&crc, seq_rr_ddcmp));
    Serial1.write(calc_crc_16(&crc, 0x00));
    Serial1.write(calc_crc_16(&crc, 0x01));
    Serial1.write(crc % 256);
    Serial1.write(crc / 256); // Transmitiu ACK

    if (last_package) {

      crc = 0;
      Serial1.write(calc_crc_16(&crc, 0x81));
      Serial1.write(calc_crc_16(&crc, 0x02));         // nn
      Serial1.write(calc_crc_16(&crc, 0x40));         // mm
      Serial1.write(calc_crc_16(&crc, seq_rr_ddcmp)); // rr
      Serial1.write(calc_crc_16(&crc, 0x03));         // xx
      Serial1.write(calc_crc_16(&crc, 0x01));         // sadd
      Serial1.write(crc % 256);
      Serial1.write(crc / 256); // Transmitiu DATA HEADER

      Serial1.write(0x77);
      Serial1.write(0xFF);
      Serial1.write(0x67);
      Serial1.write(0xB0);   // Transmitiu FINIS

      if (Serial1.readBytes (buffer_rx, 8) != 8 ) {
        break;
      }
      if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
        break;
      } // ACK

      break;
    }

    delay( 10 );

  } while (true);

  fileEva_dts.close();
}

char calc_crc_16(unsigned int *pCrc, unsigned char uData) {

  char oldData = uData;

  int iBit;

  for (iBit = 0; iBit < 8; iBit++, uData >>= 1) {

    if ((uData ^ *pCrc) & 0x01) {

      *pCrc >>= 1;
      *pCrc ^= 0xA001;

    } else
      *pCrc >>= 1;
  }

  return oldData;
}

void readTelemetryDex() {
   
   HardwareSerial Serial1 = HardwareSerial(1);

  Serial1.begin( 9600, SERIAL_8N1, pin_dex_rx, pin_dex_tx, true  );
  Serial1.setTimeout( 3000 );

  //----- First Handshake ---------------------------------
  Serial1.write( 0x05 ); // ENQ

  if ( !Serial1.find( "\x10\x30" ) ) {
    return;
  } // DLE '0'

  delay(10); // ticks para ms

  unsigned int crc;

  crc = 0;
  Serial1.write( 0x10 );                      // DLE
  Serial1.write( 0x01 );                      // SOH
  Serial1.write( calc_crc_16( &crc, '1' ) );
  Serial1.write( calc_crc_16( &crc, '2' ) );
  Serial1.write( calc_crc_16( &crc, '3' ) );
  Serial1.write( calc_crc_16( &crc, '4' ) );
  Serial1.write( calc_crc_16( &crc, '5' ) );
  Serial1.write( calc_crc_16( &crc, '6' ) );
  Serial1.write( calc_crc_16( &crc, '7' ) );
  Serial1.write( calc_crc_16( &crc, '8' ) );
  Serial1.write( calc_crc_16( &crc, '9' ) );
  Serial1.write( calc_crc_16( &crc, '0' ) );
  Serial1.write( calc_crc_16( &crc, 'R' ) );
  Serial1.write( calc_crc_16( &crc, 'R' ) );
  Serial1.write( calc_crc_16( &crc, '0' ) );
  Serial1.write( calc_crc_16( &crc, '0' ) );
  Serial1.write( calc_crc_16( &crc, 'L' ) );
  Serial1.write( calc_crc_16( &crc, '0' ) );
  Serial1.write( calc_crc_16( &crc, '6' ) );
  Serial1.write( 0x10 );                      // DLE
  Serial1.write( calc_crc_16( &crc, 0x03 ) ); // ETX
  Serial1.write( crc % 256 );
  Serial1.write( crc / 256 );

  if ( !Serial1.find( "\x10\x31" ) ) {
    return;
  } // DLE '1'

  delay(10);

  Serial1.write( 0x04 ); // EOT

  //--- Second Handshake -------------------------------
  if ( !Serial1.find( "\x05" ) ) {
    return;
  } // ENQ
  vTaskDelay(pdMS_TO_TICKS(10)); // ticks para ms

  Serial1.write( "\x10\x30" ); // DLE '0'

  // <--- (DLE SOH) & Communication ID & Response Code & Revision&Level & (DLE ETX CRC-16)
  if ( !Serial1.find( "\x10\x01" ) ) {
    return;
  } // DLE SOH

  // ...

  if ( !Serial1.find( "\x10\x03" ) ) {
    return;
  } // DLE ETX

  delay(10);

  Serial1.write( "\x10\x31" ); // DLE '1'

  if ( !Serial1.find( "\x04" ) ) {
    return;
  } // EOT

  //--- Data Transfer -------------------------------
  if ( !Serial1.find( "\x05" ) ) {
    return;
  } // ENQ

  File fileEva_dts = SPIFFS.open(TELEMETRY_FILE, FILE_WRITE);

  byte b = 0x00;
  do {

    vTaskDelay(pdMS_TO_TICKS(100)); // ticks para ms

    // clear buffer...
    while ( Serial1.available() ) Serial1.read();

    Serial1.write( 0x10 ); // DLE
    Serial1.write( 0x30 | (b++ & 0x01)); // '0'|'1'

    // <--- (DLE STX) & (Audit Data (Block n)) & (DLE ETB|ETX CRC-16)

    // DLE STX
    if ( Serial1.find( "\x10\x02" ) ) {

      fileEva_dts.print( Serial1.readStringUntil( '\x10' ) );

    } else {

      // EOT
      break;
    }

  } while (true);

  fileEva_dts.close();
}

class CharacteristicMainCallback: public BLECharacteristicCallbacks {

    void onWrite(BLECharacteristic *pCharacteristic) {

//      if ( sizeof( pCharacteristic->getData() ) == 3 && pCharacteristic->getData()[0] == 'd' && pCharacteristic->getData()[1] == 'e' && pCharacteristic->getData()[2] == 'x' ) {
//        toReadtelemetryDEX = true;
//      }else if ( sizeof( pCharacteristic->getData() ) == 5 && pCharacteristic->getData()[0] == 'd' && pCharacteristic->getData()[1] == 'd' && pCharacteristic->getData()[2] == 'c' && pCharacteristic->getData()[3] == 'm' && pCharacteristic->getData()[4] == 'p') {
//        toReadtelemetryDDCMP = true;
//      }

      switch ( pCharacteristic->getData()[0] ) {

        case 'a': {

            toReadtelemetryDDCMP = true;
          }
          break;

        case 'b': {

            toReadtelemetryDEX = true;
          }
          break;
      }
    }
};

void setup() {

  ledcAttachPin(pin_led_r, 0); // Atribuimos o pino r ao canal 0.
  ledcSetup(0, 1000, 9); // Atribuimos ao canal 0 a frequencia de 1000Hz com resolucao de 9bits.

  ledcAttachPin(pin_led_g, 1);
  ledcSetup(1, 1000, 11);

  ledcAttachPin(pin_led_b, 2);
  ledcSetup(2, 1000, 9);

  sendRGBtoLed( 0x0000ff );

  SPIFFS.begin(true);
  SPIFFS.format();

  BLEDevice::init( "BLE-EVA" );

  BLEServer *bleServer = BLEDevice::createServer();

  BLEService *bleMainService = bleServer->createService( "ffe0" );

  mCharacteristic = bleMainService->createCharacteristic( "ffe1", BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  mCharacteristic->setCallbacks(new CharacteristicMainCallback());

  BLE2902 *bLE2902 = new BLE2902();
  bLE2902->setNotifications(true);

  mCharacteristic->addDescriptor( bLE2902 );

  // Start the service...
  bleMainService->start();

  // Start advertising... (Descoberta do ESP32)
  BLEAdvertising *bleAdvertising = BLEDevice::getAdvertising();
  bleAdvertising->addServiceUUID( "ffe0" );
  bleAdvertising->setScanResponse(true);
  bleAdvertising->start( );
}

void sendRGBtoLed( int pRgbColor ) {

  ledcWrite( 2, (pRgbColor >> 0) & 0xFF );
  ledcWrite( 1, (pRgbColor >> 8) & 0xFF );
  ledcWrite( 0, (pRgbColor >> 16) & 0xFF );
}

void loop() {

  if ( toReadtelemetryDDCMP ) {
    toReadtelemetryDDCMP = false;

    mCharacteristic->setValue( "\nReading DDCMP...\n" );
    mCharacteristic->notify( );

    readTelemetryDdcmp();

    File fileEva_dts = SPIFFS.open(TELEMETRY_FILE, FILE_READ);
    if ( fileEva_dts && !fileEva_dts.isDirectory() ) {

      byte buff[15];
      int length_ = 0;

      do {

        length_ = fileEva_dts.read( buff, 16 );

        mCharacteristic->setValue( buff, length_ );
        mCharacteristic->notify( );

        delay( 5 );
      } while ( length_ > 0 );

      SPIFFS.remove(TELEMETRY_FILE);
    } else fileEva_dts.close();

    mCharacteristic->setValue( "\nEnd." );
    mCharacteristic->notify( );
  }

  if ( toReadtelemetryDEX ) {
    toReadtelemetryDEX = false;

    mCharacteristic->setValue( "\nReading DEX...\n" );
    mCharacteristic->notify( );

    readTelemetryDex();

    File fileEva_dts = SPIFFS.open(TELEMETRY_FILE, FILE_READ);
    if ( fileEva_dts && !fileEva_dts.isDirectory() ) {

      byte buff[15];
      int length_ = 0;

      do {

        length_ = fileEva_dts.read( buff, 16 );

        mCharacteristic->setValue( buff, length_ );
        mCharacteristic->notify( );

        delay( 5 );
      } while ( length_ > 0 );

      SPIFFS.remove(TELEMETRY_FILE);
    } else fileEva_dts.close();

    mCharacteristic->setValue( "\nEnd." );
    mCharacteristic->notify( );
  }

}
