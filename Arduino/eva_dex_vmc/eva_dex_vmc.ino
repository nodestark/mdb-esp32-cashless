
#define pin_led_r           19
#define pin_led_g           21
#define pin_led_b           23

int pa201= 0;

void setup() {
  
  ledcAttachPin(pin_led_r, 0); // Atribuimos o pino r ao canal 0.
  ledcSetup(0, 1000, 9); // Atribuimos ao canal 0 a frequencia de 1000Hz com resolucao de 9bits.

  ledcAttachPin(pin_led_g, 1);
  ledcSetup(1, 1000, 11);

  ledcAttachPin(pin_led_b, 2);
  ledcSetup(2, 1000, 9);

  sendRGBtoLed( 0x0000ff ); 
  
  Serial1.begin( 9600, SERIAL_8N1, 17, 16 );

  Serial1.setTimeout( 2000 );    

}

void loop() {  

  sendRGBtoLed( 0x00ff00 ); 

  // First Handshake  
  if( !Serial1.find( "\x0005" ) ){
    return;
  }

  sendRGBtoLed( 0x0000ff ); 

  Serial1.write( 0x10 );
  Serial1.write( '0' );

  if( !Serial1.find( "\x0010\x0001" ) ){
    return;
  } // DLE SOH
  // ...
  if( !Serial1.find( "\x0010\x0003" ) ){
    return;      
  } // DLE ETX

  Serial1.write( 0x10 );
  Serial1.write( '1' );

  if( !Serial1.find( "\x0004" ) ){
    return;
  } // EOT

  // Seccond Handshake
  Serial1.write( 0x05 );

  if( !Serial1.find( "\x0010\x0030" ) ){

    return;
  } // DLE '0'

  Serial1.write( 0x10 );  
  Serial1.write( 0x01 );
  Serial1.write( "1234567890" );
  Serial1.write( "00" );  
  Serial1.write( "R00L06" );
  Serial1.write( 0x10 );
  Serial1.write( 0x03 );
  Serial1.write( 0xff );
  Serial1.write( 0xff );

  if( !Serial1.find( "\x0010\x0031" ) ){

    return;
  } // DLE '1'

  Serial1.write( 0x04 );

  // Data Transfer  
  Serial1.write( 0x05 );

  boolean block= false;

  for( int x= 1; x < 128; ){ //aumentar o tamanho 1024 = 28kb

    if( !block && !Serial1.find( "\x0010\x0030" ) ){

      return;
    } // DLE '0'
    if( block && !Serial1.find( "\x0010\x0031" ) ){

      return;
    } // DLE '1'
    block= !block;

    // Block n
    Serial1.write( 0x10 );
    Serial1.write( 0x02 );  

    Serial1.print( "PA1*" );
    Serial1.print( x++ );
    Serial1.print( "*100****\x000D\x000A" );
    Serial1.print( "PA2*0*0*0*0\x000D\x000A" );
    delay(50);
    Serial1.print( "PA1*" );
    Serial1.print( x++ );
    Serial1.print( "*200****\x000D\x000A" );
    Serial1.print( "PA2*0*0*0*0\x000D\x000A" );
    delay(50);
    Serial1.print( "PA1*" );
    Serial1.print( x++ );
    Serial1.print( "*300****\x000D\x000A" );
    Serial1.print( "PA2*0*0*0*0\x000D\x000A" );
    delay(50);
    Serial1.print( "PA1*" );
    Serial1.print( x++ );
    Serial1.print( "*400****\x000D\x000A" );
    Serial1.print( "PA2*0*0*0*0\x000D\x000A" );
    delay(50);
    Serial1.write( 0x10 );
    Serial1.write( 0x17 );
    Serial1.write( 0xffff );

  }

  if( !Serial1.find( "\x0010\x0030" ) ){

    return;
  } // DLE '0'

  // Block last one
  Serial1.write( 0x10 );
  Serial1.write( 0x02 );
  Serial1.print( "PA1*65*500****\x000D\x000A" );
  Serial1.print( "PA2*" );
  Serial1.print( pa201++ );
  Serial1.print( "*0*0*0\x000D\x000A" );
  delay(50);  
  Serial1.print( "EA3**000101*000080**000101*000080***490*490\x000D\x000A" );
  delay(50);
  Serial1.print( "G85*1BB8\x000D\x000A" );
  Serial1.print( "SE*280*0001\x000D\x000A" );
  delay(50);
  Serial1.print( "DXE*1*1\x000D\x000A" );
  Serial1.write( 0x10 );
  Serial1.write( 0x03 );
  Serial1.write( 0xffff );
  // EOT

  if( !Serial1.find( "\x0010\x0031" ) ){

    return;
  } // DLE '1'

  Serial1.write( 0x04 );
}

void sendRGBtoLed( unsigned long pRgbColor ) {

  ledcWrite( 2, (pRgbColor >> 0) & 0xFF );
  ledcWrite( 1, (pRgbColor >> 8) & 0xFF );
  ledcWrite( 0, (pRgbColor >> 16) & 0xFF );
}
