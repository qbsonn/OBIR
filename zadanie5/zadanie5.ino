#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <stdlib.h>
#include <string.h>
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


RF24 radio(7, 8);               // nRF24L01(+) radio attached using Getting Started board
RF24Network network(radio);      // Network uses that radio

const uint16_t this_node = 00;    // Address of our node in Octal format ( 04,031, etc)
const uint16_t other_node = 01;


struct payload_t {                 // Structure of our payload
	unsigned long ms;
	unsigned short value;
};
IPAddress ip(192, 168, 2, 140);

unsigned short valueP;

struct Option
{
	uint16_t optionType;
	uint16_t optionLength;
	byte *optionValue;
};

enum uriPaths
{
  WELL_KNOWN_CORE = 1, POTENTIOMETR = 2, LAMP = 3
};

struct CoapPacket
{
	byte ver;
	byte type;
	byte tokenLength;
	byte code;
	byte messageID[2];
	byte *token;
	byte optionsNumber;
	Option *options;
	byte payloadLength; // nwm czy potrzebne
	byte *payload;
};

struct CoapResponse
{
	byte optLen;
	byte optTyp;
	byte *optValue;
};

enum acceptedFormat
{
	PLAIN = 0, LINKFORMAT = 40
};

enum optionTypes
{
	URI_PATH = 11, CONTENT_FORMAT = 12, ACCEPT = 17
};

int are_equal(const char *c1, const char *c2, byte c1Size, byte c2Size)
{
    if (c1Size !=c2Size)
        return 1; // They must be different
    for (int i = 0; i < c1Size; i++)
    {
        if (c1[i] != c2[i])
            return 1;  // They are different
    }
    return 0;  // They must be the same
}

byte packetBuffer[MAX_BUFFER];

void setup() {
	Serial.begin(115200);
	Ethernet.begin(mac, ip);
	Serial.println(Ethernet.localIP());
	udp.begin(localPort);

	//czesc radiowa
	SPI.begin();
	radio.begin();
	network.begin(30, this_node);
	Serial.println("Setup finished");
}

void loop() {
	int packetSize = udp.parsePacket();

	if (packetSize) {
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
void receivePacket() {
	// byte packetBuffer[MAX_BUFFER];
	int packetLength = udp.read(packetBuffer, MAX_BUFFER);

	Serial.println();
	Serial.println("[ODEBRANO PAKIET]");
	Serial.print("Dlugosc pakietu[B]: ");
	Serial.println(packetLength);

	CoapPacket cPacket;

	cPacket.ver = packetBuffer[0] >> 6; // Bity numer 0, 1
	cPacket.type = byte(packetBuffer[0] << 2) / 64; // Bity numer 2, 3
	cPacket.tokenLength = byte(packetBuffer[0] << 4) / 16; // Bity numer 4, 5, 6, 7
	cPacket.code = packetBuffer[1];
	cPacket.messageID[0] = packetBuffer[2];
	cPacket.messageID[1] = packetBuffer[3];

	if ( cPacket.ver != 1) {
		Serial.println("Zły format nagłówka");
		return;
	}

	byte tokenTab[cPacket.tokenLength];
	for (int i = 0; i < cPacket.tokenLength; i++) {
		tokenTab[i] = packetBuffer[4 + i];
	}
	cPacket.token = tokenTab;

	byte optionHeader; // pierwszy bajt opcji
	int optionCounter = 0; // zlicza opcje
	int currentByteNumber = 4 + cPacket.tokenLength;
	byte prevOptionType=0; // potrzebne żeby dodać do option delta
	bool isNotPayloadByte = true;

	Option options[6]; // zakladam ze jest max 6 opcji
	cPacket.options = options;

	while((currentByteNumber < packetLength) && isNotPayloadByte)
	{
		if (packetBuffer[currentByteNumber] == 255) // bajt samych jedynek rozpoczynający payload
		{
			isNotPayloadByte = false;
		}
		else {
//			cPacket.options = realloc(cPacket.options, (optionCounter+1)*sizeof(Option));

			optionHeader = packetBuffer[currentByteNumber++];

			byte optType = byte(optionHeader >> 4);
			byte optLen = byte(optionHeader << 4) / 16;

			if (optType > 12) // Option type extension
			{
				if (optType - 12 == 2) { // Type extension 2 bytes
					optType += packetBuffer[currentByteNumber++];
					optType += packetBuffer[currentByteNumber++];
				}
				else { // Type extension 1 byte
					optType += packetBuffer[currentByteNumber++];
				}
			}

			if (optLen > 12) // Option length extension
			{
				if (optLen - 12 == 2) { // Length extension 2 bytes
					optLen += packetBuffer[currentByteNumber++];
					optLen += packetBuffer[currentByteNumber++];
				}
				else { // Length extension 1 byte
					optLen += packetBuffer[currentByteNumber++];
				}
			}

			cPacket.options[optionCounter].optionType = optType + prevOptionType;
			cPacket.options[optionCounter].optionLength = optLen;
			cPacket.options[optionCounter].optionValue = (byte*) malloc(optLen * sizeof(byte*));
			if (cPacket.options[optionCounter].optionValue == NULL)
				Serial.println("lipa z pamiecia optValue");
			memcpy(cPacket.options[optionCounter].optionValue, packetBuffer+currentByteNumber, optLen);


			prevOptionType += optType;
			currentByteNumber += optLen;
			optionCounter++;
		}
	}

	// KONIEC WCZYTYWANIA OPCJI
	cPacket.optionsNumber = optionCounter;

	// WCZYTYWANIE PAYLOADU
	if ((packetLength > currentByteNumber) && (packetBuffer[currentByteNumber] == 255)) {
		currentByteNumber++;
		cPacket.payloadLength = packetLength - currentByteNumber;
//		byte payload[cPacket.payloadLength];
//		memcpy(payload, packetBuffer+currentByteNumber, cPacket.payloadLength);
//		cPacket.payload = payload;
		cPacket.payload = (byte*) malloc(cPacket.payloadLength * sizeof(*cPacket.payload));
		memcpy(cPacket.payload, packetBuffer+currentByteNumber, cPacket.payloadLength);
	}
	else {
		cPacket.payloadLength = 0;
	}

	//printCoapPacket(&cPacket);
	handlePacket(&cPacket);

	// Zwolnienie pamieci po obsludze pakietu
	if (cPacket.optionsNumber > 0) {
		for (int i=0; i<cPacket.optionsNumber; i++) {
			if (cPacket.options[i].optionLength > 0)
				free(cPacket.options[i].optionValue);
		}
		//free(cPacket.options);
	}
	if (cPacket.payloadLength >0)
		free(cPacket.payload);
}

void handlePacket(CoapPacket *cPacket)
{
	/*if (optType == ACCEPT)
			{
				int optionValue = 0;
				if (optLen == 0) //plaintext
				{
					optionValue = 0;
					//zapamietanie
				}
				else
				{
					for (int i = 0; i < optLen; i++)
					{
						optionValue += packetBuffer[currentByteNumber];
					}

					if (optionValue == LINKFORMAT)
					{
						//zapamietanie
					}
					else
					{
						//error nieobslugiwany format
					}
				}
			}*/
     
	if (cPacket->type == 0 && cPacket->code == 0) //Ping
	{
		Serial.println("ping");
		responseForPing(cPacket);
	}
	else if (cPacket->type == 1 && cPacket->code == 1) 	// GET
	{
		Serial.println("GET");
		responseForGet(cPacket);
	}

}

void responseForGet(CoapPacket *cPacket)
{

Serial.println(are_equal("well","ore",sizeof("well"),sizeof("ore")));
//Serial.println(are_equal("well","welll"));
Serial.println(are_equal("well","weuy",sizeof("well"),sizeof("weyu")));
Serial.println(are_equal("well","well",sizeof("well"),sizeof("well")));
 // sendGetPotentiometrValueMessage();
  bool errorFormatFlag=false;
  byte uriPathType = 0;
  
	CoapPacket responsePacket;
  responsePacket.optionsNumber = 0;
  responsePacket.payloadLength = 0;

  for(byte i=0; i< cPacket->optionsNumber; i++){
          char ddd[5] = "well";

         
          
          

    switch(cPacket->options[i].optionType)
    {
      case URI_PATH: Serial.println("URI");
        if (are_equal(cPacket->options[i].optionValue, ".well-known",cPacket->options[i].optionLength, 11 ) == 0){
          //Serial.println("WSZEDL WELL");
          if ((are_equal(cPacket->options[i+1].optionValue, "core",cPacket->options[i+1].optionLength, 4) == 0)){
            uriPathType = WELL_KNOWN_CORE;
            //Serial.println("WSZEDL WELL1");
          }
          
        }
        else if (are_equal(cPacket->options[i].optionValue, "potentiometr",cPacket->options[i].optionLength, 12 ) == 0){
          Serial.println("potentio");
          uriPathType = POTENTIOMETR;
        }
        
        //Serial.println (strcmp(cPacket->options[i].optionValue, ".well-known"));
        
        //Serial.println((char)cPacket->options[i].optionValue);
        
        break;

      case CONTENT_FORMAT:Serial.println("FORMAT");
      break;

      case ACCEPT:
      Serial.println("ACCEPT");
      if (cPacket->options[i].optionValue[0] != PLAIN)
     {
        Serial.println("Zły format");
        errorFormatFlag=true;
      }
      break;
    }
  }
  
    // HEADER
  responsePacket.ver = 1;
  responsePacket.type = 1;
  responsePacket.tokenLength = cPacket->tokenLength;
  responsePacket.messageID[0] = cPacket->messageID[0];
  responsePacket.messageID[1] = cPacket->messageID[1] + 1;
  // TOKEN
  responsePacket.token = cPacket->token;

  Serial.print("Token: "); Serial.println(cPacket->token[0]);

  
  if (! errorFormatFlag)
  {
	
  	responsePacket.code = (byte)69; //2.05

    if (uriPathType == WELL_KNOWN_CORE){
      Serial.println ("WELL KNOWN");
      responsePacket.optionsNumber =1;
      Option options[responsePacket.optionsNumber];

      options[0].optionType = 12;
      options[0].optionLength = 1;
      options[0].optionValue = (byte*) malloc(options[0].optionLength * sizeof(byte*));
      options[0].optionValue[0] = 40; //core link format 

      responsePacket.options = options;

      responsePacket.payloadLength = 86;
      char payload[] = "</potentiometr>;rt=\"Potentiometr value\";ct=0;if=\"sensor\";,</lamp>;rt=\"Lamp value\";ct=0";
      
      Serial.print("payload length: ");Serial.println(sizeof(payload)/sizeof(char));

      responsePacket.payload =payload;
    }
    else if (uriPathType == POTENTIOMETR){
      
      uint16_t receiveValue=888;//receivePotentiometrValueFromMini();
      Serial.print ("Odebrane: ");
      Serial.println(receiveValue);
      
      responsePacket.optionsNumber =1;
      Option options[responsePacket.optionsNumber];
  
        responsePacket.optionsNumber =1;
        options[0].optionType = 12;
        options[0].optionLength = 0;
  
        responsePacket.options = options;
  //      options[0].optionValue = (byte*) malloc(options[0].optionLength * sizeof(byte*));
  //      options[0].optionValue[0] = 1;
  
    
      if (receiveValue >= 1000)
        responsePacket.payloadLength = 4;
      else if (receiveValue >= 100)
        responsePacket.payloadLength = 3;
       else if (receiveValue >= 10)
        responsePacket.payloadLength = 2;
         else
        responsePacket.payloadLength = 1;
    
        byte payload[responsePacket.payloadLength];
        uint16_t prevValue = 0;
        for (byte i=0; i<responsePacket.payloadLength; i++ ){
          payload[i] = receiveValue / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
          prevValue = (prevValue + payload[i]) * 10;
          payload[i] += '0';
          Serial.print("payload : "); Serial.println(payload[i]);
        }
    
      responsePacket.payload = payload;
    }
    else{
      responsePacket.code = (byte)132; //4.04 
    responsePacket.payloadLength = 14;
    char diagnosticPayload[responsePacket.payloadLength] = "Wrong URI-PATH";

    responsePacket.payload = diagnosticPayload;
   
    }
  	
 	} 
  else
  {
    responsePacket.code = (byte)143; //4.15  
    responsePacket.payloadLength = 22;
    char diagnosticPayload[responsePacket.payloadLength] = "Accepted format: plain";

    responsePacket.payload = diagnosticPayload;
        
  }

  sendResponse(&responsePacket);

  // Zwolnienie pamieci z optionValue
  for (byte i=0; i<responsePacket.optionsNumber; i++ ){
    if (responsePacket.options[i].optionLength > 0)
        free(responsePacket.options[i].optionValue);
  }
}


void responseForPing(CoapPacket *cPacket)
{
	CoapPacket responsePacket;

	responsePacket.ver = 1;
	responsePacket.type = 2;
	responsePacket.code = (byte)64;
	responsePacket.tokenLength = cPacket->tokenLength;
	// responsePacket.token=cPacket.token;
	responsePacket.messageID[0] = cPacket->messageID[0];
	responsePacket.messageID[1] = cPacket->messageID[1];

	responsePacket.optionsNumber = 0;
	responsePacket.payloadLength = 0;

	sendResponse(&responsePacket);
}



unsigned short getPotentiometrValueOptionSelected()
{
	Serial.println("[odbior wartosci otencjometru]");
	sendGetPotentiometrValueMessage();
	currentValue = receivePotentiometrValueFromMini();
	Serial.println(currentValue);
	return currentValue;
}


bool sendGetPotentiometrValueMessage() {

	network.update();

	payload_t payload = { millis(), 11};
	RF24NetworkHeader header(/*to node*/ other_node);
	bool ok = network.write(header, &payload, sizeof(payload));

	return ok;

}

unsigned short receivePotentiometrValueFromMini() {
	if ( network.available() )
	{

		RF24NetworkHeader header;
		payload_t payload;
		network.read(header, &payload, sizeof(payload));
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

void printCoapPacket(CoapPacket *cPacket) {
	Serial.println();
	Serial.print("Ver:\t");
	Serial.println(cPacket->ver);
	Serial.print("Type:\t");
	Serial.println(cPacket->type);
	Serial.print("TKL:\t");
	Serial.println(cPacket->tokenLength);
	Serial.print("Code:\t");
	Serial.println(cPacket->code);
	Serial.print("Token:\t");
	if (cPacket->tokenLength == 0)
		Serial.print("empty");
	else {
		Serial.print("0x");
		for (int i=0; i< cPacket->tokenLength; i++) {
			Serial.print(cPacket->token[i], HEX);
		}
	}
	Serial.println();
	Serial.print("Option number:\t");
	Serial.println(cPacket->optionsNumber);
	for (int i=0; i< cPacket->optionsNumber; i++)
	{
		Serial.print("Options: ");
		Serial.print(i);
		Serial.print("\tLength: ");
		Serial.print(cPacket->options[i].optionLength);
		Serial.print("\tType: ");
		Serial.print(cPacket->options[i].optionType);
		Serial.print("\tValue: ");
		if (cPacket->options[i].optionLength == 0)
			Serial.print("empty");
		else
			for (int j=0; j<cPacket->options[i].optionLength; j++) {
				Serial.print(*(cPacket->options[i].optionValue+j));
				Serial.print(" ");
			}
		Serial.println();
	}
	Serial.print("Payload length:\t");
	Serial.println(cPacket->payloadLength);
	Serial.print("Payload:\t");
	if (cPacket->payloadLength == 0)
		Serial.print("empty");

	else
		for (int i=0; i< cPacket->payloadLength; i++) {
			Serial.print(cPacket->payload[i]);
			Serial.print(" ");
		}
	Serial.println();
	Serial.println();
}


void sendResponse(CoapPacket *cPacket) {
	udp.beginPacket(udp.remoteIP(), udp.remotePort());

	int packetSize = calculateCoapPacketSize(cPacket);
	Serial.print("Packet size: ");
	Serial.println(packetSize);
	//byte message[4 + cPacket->tokenLength];
	byte message[packetSize];
	byte currentByteNumber = 4;
	byte x = 1;
	x = x << 2;
	x = x + cPacket->type;
	x = x << 4;
	x = x + cPacket->tokenLength;

	message[0] = x;
	message[1] = cPacket->code;
	message[2] = cPacket->messageID[0];
	message[3] = cPacket->messageID[1];

	//Token
	for (int i = 0; i < cPacket->tokenLength; i++)
	{
		message[currentByteNumber++] = cPacket->token[i];
	}

	//Options
	if(cPacket->optionsNumber > 0)
	{
		uint16_t optionDelta = 0;
		for (int i=0; i<cPacket->optionsNumber; i++) {

			byte optionHeaderByteNumber = currentByteNumber++;
			byte optionHeader = 0;
			uint16_t optionTypeMinusDelta = cPacket->options[i].optionType - optionDelta;
			optionDelta = cPacket->options[i].optionType;

			if (optionTypeMinusDelta >= 269){ // 2 bytes extension type
				optionHeader = 14;
				message[currentByteNumber++] = byte((optionTypeMinusDelta - 14) >> 8);
				message[currentByteNumber++] = byte(optionTypeMinusDelta - 14);
			}
			else if (optionTypeMinusDelta >= 13){ // 1 byte extension type
				optionHeader = 13;
				message[currentByteNumber++] = byte(optionTypeMinusDelta - 13);
			}
			else { // no extension type
				optionHeader = optionTypeMinusDelta;
			}
			optionHeader = optionHeader << 4;
			 
			if (cPacket->options[i].optionLength >= 269){ // 2 bytes extension length
				optionHeader += 14;
				message[currentByteNumber++] = byte((cPacket->options[i].optionLength - 14) >> 8);
				message[currentByteNumber++] = byte(cPacket->options[i].optionLength - 14);
			}
			else if (cPacket->options[i].optionLength >= 13){ // 1 byte extension length
				optionHeader += 13;
				message[currentByteNumber++] = byte(cPacket->options[i].optionLength - 13);
			}
			else { // no extension type
				optionHeader += cPacket->options[i].optionLength;
			}
	
			message[optionHeaderByteNumber] = optionHeader;
	
			memcpy(message+currentByteNumber, cPacket->options[i].optionValue, cPacket->options[i].optionLength);
			currentByteNumber += cPacket->options[i].optionLength;
		}
	}

	// Payload
	if (cPacket->payloadLength > 0){
		message[currentByteNumber++] = 255; // bajt jedynek
		memcpy(message+currentByteNumber, cPacket->payload, cPacket->payloadLength);
	}

	int len = udp.write(message, sizeof (message));

	udp.endPacket();
}

int calculateCoapPacketSize(CoapPacket *cPacket) {
	int size = 4; // naglowek
	size += cPacket->tokenLength;

	uint16_t optionDelta = 0;
	for (int i=0; i<cPacket->optionsNumber; i++) {
		size++; // OptionType, OptionLength bez extension

		uint16_t optionTypeMinusDelta = cPacket->options[i].optionType - optionDelta;
		optionDelta = cPacket->options[i].optionType;
		
		if (optionTypeMinusDelta >= 269) // 2 bytes extension type
			size += 2;
		else if (optionTypeMinusDelta >= 13) // 1 byte extension type
			size++;

		if (cPacket->options[i].optionLength >= 269) // 2 bytes extension length
			size += 2;
		else if (cPacket->options[i].optionLength >= 13) // 1 byte extension length
			size++;

		size += cPacket->options[i].optionLength;
	}

	if (cPacket->payloadLength > 0) { // jeśli tak to payload istnieje
		size++; // bajt samych jedynek
		size += cPacket->payloadLength;
	}
	
	return size;
}



