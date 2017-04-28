#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <stdlib.h>

#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

byte mac[] = {
  0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0xFA
};

EthernetUDP udp;

const int MAX_BUFFER = 255;
int currentValue = 255;

short localPort = 1244;


RF24 radio(7,8);                // nRF24L01(+) radio attached using Getting Started board 
RF24Network network(radio);      // Network uses that radio

const uint16_t this_node = 00;    // Address of our node in Octal format ( 04,031, etc)
const uint16_t other_node = 01; 


struct payload_t {                 // Structure of our payload
  unsigned long ms;
  unsigned short value;
};
IPAddress ip(192, 168, 2, 140);

unsigned short valueP;

struct coapHeader
{
  byte ver;
  byte type;
  byte tokenLength;
  byte code;
  byte  messageID[2];
  byte *token;
};

  

void setup() {
  Serial.begin(115200);
  Ethernet.begin(mac,ip);
  Serial.println(Ethernet.localIP());
  udp.begin(localPort);
  
  //czesc radiowa
  SPI.begin();
  radio.begin();
  network.begin(30,this_node);
  Serial.println("Setup finished");
}

void loop() {
  int packetSize = udp.parsePacket();
  
  if(packetSize){
    receivePacket();
  }
}


/*
void receivePacket(){
    char packetBuffer[MAX_BUFFER];
    udp.read(packetBuffer, MAX_BUFFER);

  Serial.println("message");
    if (packetBuffer[0]=='g')
    {
        unsigned short potentiometrValue = getPotentiometrValueOptionSelected();
        sendResponse(potentiometrValue);
    }
    
}
*/
void receivePacket(){
   byte packetBuffer[MAX_BUFFER];
    udp.read(packetBuffer, MAX_BUFFER);

    Serial.println("message");
    /*
    if (packetBuffer[0]=='g')
    {
        unsigned short potentiometrValue = getPotentiometrValueOptionSelected();
        sendResponse(potentiometrValue);
    }
    */


    //Trzeba uporzadkowac!!!
     coapHeader cHeader;
   
     cHeader.ver=packetBuffer[0]>>6;
  
     cHeader.type=byte(packetBuffer[0]<<2)/64;
     cHeader.tokenLength=byte(packetBuffer[0]<<4)/16;
    
      cHeader.code=packetBuffer[1];
     cHeader.messageID[0]=packetBuffer[2];
     cHeader.messageID[1]=packetBuffer[3];

    if (cHeader.tokenLength>0)
    {
      byte tokentab[cHeader.tokenLength];
      for (int i=0; i<cHeader.tokenLength; i++)
      {
        tokentab[i]=packetBuffer[4+i];
        Serial.println(tokentab[i]);
      }
      
      cHeader.token=tokentab;
      Serial.println("CHEADER");
          Serial.println(*cHeader.token);
      
    }
  
if( cHeader.ver!=1)
  {
Serial.println("Zły format nagłówka");
  }



  if (cHeader.type==0&&cHeader.code==0)
  {//Ping
    Serial.println("ping");
    responseForPing(cHeader);
  }

  else if (cHeader.type==1&&cHeader.code==1)
  {

    Serial.println("GET");

    responseForGet(&cHeader);
    
  }

}

void responseForGet(coapHeader *cHeader)
{
 coapHeader responseHeader;
 
    responseHeader.ver= 1;
    responseHeader.type=1;
    responseHeader.code=(byte)69; //2.0.5
    responseHeader.tokenLength=cHeader->tokenLength;
    responseHeader.token=cHeader->token;
     Serial.println("CHEADER");
          //Serial.println(cHeader.token);
          Serial.println("ReSHEADER");
    Serial.println(*responseHeader.token);
    responseHeader.messageID[0]=cHeader->messageID[0];
    responseHeader.messageID[1]=cHeader->messageID[1]+1;

    sendResponse(responseHeader);

  
}


void responseForPing(coapHeader cHeader)
{
  coapHeader responseHeader;
 
    responseHeader.ver= 1;
    responseHeader.type=2;
    responseHeader.code=(byte)64;
    responseHeader.tokenLength=cHeader.tokenLength;
    responseHeader.token=cHeader.token;
    responseHeader.messageID[0]=cHeader.messageID[0];
    responseHeader.messageID[1]=cHeader.messageID[1];

    sendResponse(responseHeader);
}



unsigned short getPotentiometrValueOptionSelected()
{
  Serial.println("[odbior wartosci otencjometru]");
  sendGetPotentiometrValueMessage();
  currentValue=receivePotentiometrValueFromMini();
  Serial.println(currentValue);
  return currentValue;
}


bool sendGetPotentiometrValueMessage() {

  network.update();  

  payload_t payload = { millis(),11};
  RF24NetworkHeader header(/*to node*/ other_node);
  bool ok = network.write(header,&payload,sizeof(payload));

  return ok;

}

unsigned short receivePotentiometrValueFromMini(){
  if ( network.available() ) { 
    
    RF24NetworkHeader header;       
    payload_t payload;
    network.read(header,&payload,sizeof(payload));
    Serial.print("hahah ");
    Serial.println(payload.value);
    return payload.value;
  }
}
/*
void sendResponse(unsigned short value){
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
 
    char response[4];
    //itoa(currentValue,response,3);
    response[0] = value/1000;
    response[1] = (value-(response[0]*1000))/100;
    response[2] = (value-(response[0]*1000) - response[1]*100)/10;
    response[3] = (value-(response[0]*1000) - response[1]*100 - response[2]*10);

    for (int i=0; i<4; i++)
      response[i]=response[i]+'0';

    int len = udp.write(response, 4);
    udp.endPacket();
}

*/

void sendResponse(coapHeader cHeader ){
  udp.beginPacket(udp.remoteIP(), udp.remotePort());

  byte message[4+cHeader.tokenLength];
  byte x=1;
  x=x<<2;
  x=x+cHeader.type;
  x=x<<4;
  x=x+cHeader.tokenLength;

  message[0]=x;
  message[1]=cHeader.code;
  message[2]=cHeader.messageID[0];
  message[3]=cHeader.messageID[1];
 for (int i=0; i<cHeader.tokenLength;i++)
 {
  message[4+i]=*(cHeader.token+i);
  Serial.println(message[4+i]);
 }
  int len = udp.write(message, sizeof (message));
  udp.endPacket();
}



