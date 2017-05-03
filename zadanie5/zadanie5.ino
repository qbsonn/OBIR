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
	byte optionType;
	byte optionLength;
	byte *optionValue;
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

	if ( cPacket.ver != 1){
		Serial.println("Zły format nagłówka");
		return;
	}

	byte tokenTab[cPacket.tokenLength];
	for (int i = 0; i < cPacket.tokenLength; i++){
		tokenTab[i] = packetBuffer[4 + i];
	}
	cPacket.token = tokenTab;
	
	byte optionHeader; // pierwszy bajt opcji
	int optionCounter = 0; // zlicza opcje
	int currentByteNumber = 4 + cPacket.tokenLength;
	byte prevOptionType=0; // potrzebne żeby dodać do option delta
	Option* options;
	bool isNotPayloadByte = true;
	
	while((currentByteNumber < packetLength) && isNotPayloadByte)
	{
		if (packetBuffer[currentByteNumber] == 255) // bajt samych jedynek rozpoczynający payload
		{
			isNotPayloadByte = false;
		}
		else {		
			options = realloc(options, (optionCounter+1)*sizeof(Option));
			optionHeader = packetBuffer[currentByteNumber++];
			
			byte optType = byte(optionHeader >> 4);
			byte optLen = byte(optionHeader << 4) / 16;

			if (optType > 12) // Option type extension
			{
				if (optType - 12 == 2){ // Type extension 2 bytes
					optType += packetBuffer[currentByteNumber++];
					optType += packetBuffer[currentByteNumber++];
				}
				else{ // Type extension 1 byte
					optType += packetBuffer[currentByteNumber++];
				}
			}

			if (optLen > 12) // Option length extension
			{
				if (optLen - 12 == 2){ // Length extension 2 bytes
					optLen += packetBuffer[currentByteNumber++];
					optLen += packetBuffer[currentByteNumber++];
				}
				else{ // Length extension 1 byte
					optLen += packetBuffer[currentByteNumber++];
				}
			}

			byte* optValue = (byte*) malloc(optLen * sizeof(*optValue));
			memcpy(optValue, packetBuffer+currentByteNumber, optLen);
			
			options[optionCounter].optionType = optType + prevOptionType;
			options[optionCounter].optionLength = optLen;
			options[optionCounter].optionValue = optValue;

			
			prevOptionType += optType;
			currentByteNumber += optLen;
			optionCounter++;
		}
	}
	
	// KONIEC WCZYTYWANIA OPCJI
	cPacket.optionsNumber = optionCounter;
	cPacket.options = options;
	
	// WCZYTYWANIE PAYLOADU
	if (packetBuffer[currentByteNumber] == 255){ 
		currentByteNumber++;
		cPacket.payloadLength = packetLength - currentByteNumber;
		cPacket.payload = (byte*) malloc(cPacket.payloadLength * sizeof(*cPacket.payload));
		memcpy(cPacket.payload, packetBuffer+currentByteNumber, cPacket.payloadLength);
	}

	printCoapPacket(&cPacket);
	handlePacket(&cPacket);
	
	
	// Zwolnienie pamieci po obsludze pakietu
	for (int i=0; i<optionCounter; i++)
		free(options[i].optionValue);
	free(options);
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
	CoapResponse coapResponse;
	
	CoapPacket responseHeader;
	CoapPacket local = *cPacket;
	responseHeader.ver = 1;
	responseHeader.type = 1;
	responseHeader.code = (byte)69; //2.05
	responseHeader.tokenLength = cPacket->tokenLength;
	// responseHeader.token=cPacket->token;
	responseHeader.token = cPacket->token;
	Serial.print("Token ");
	Serial.println(*responseHeader.token);

	responseHeader.messageID[0] = cPacket->messageID[0];
	responseHeader.messageID[1] = cPacket->messageID[1] + 1;

	sendResponse(&responseHeader);


}


void responseForPing(CoapPacket *cPacket)
{
	CoapPacket responseHeader;

	responseHeader.ver = 1;
	responseHeader.type = 2;
	responseHeader.code = (byte)64;
	responseHeader.tokenLength = cPacket->tokenLength;
	// responseHeader.token=cPacket.token;
	responseHeader.messageID[0] = cPacket->messageID[0];
	responseHeader.messageID[1] = cPacket->messageID[1];

	sendResponse(&responseHeader);
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

void printCoapPacket(CoapPacket *cPacket){
	Serial.println();
	Serial.print("Ver:\t"); Serial.println(cPacket->ver);
	Serial.print("Type:\t"); Serial.println(cPacket->type);
	Serial.print("TKL:\t"); Serial.println(cPacket->tokenLength);
	Serial.print("Code:\t"); Serial.println(cPacket->code);
	Serial.print("Token:\t0x");
	for (int i=0; i< cPacket->tokenLength; i++){
		Serial.print(cPacket->token[i], HEX);
	}
	Serial.println();
	Serial.print("Option number:\t");	Serial.println(cPacket->optionsNumber);
	for (int i=0; i< cPacket->optionsNumber; i++)
	{
		Serial.print("Options: "); Serial.print(i);
		Serial.print("\tLength: ");	Serial.print(cPacket->options[i].optionLength);
		Serial.print("\tType: ");	Serial.print(cPacket->options[i].optionType);
		Serial.print("\tValue: "); 
		for (int j=0; j<cPacket->options[i].optionLength; j++){
			Serial.print(*(cPacket->options[i].optionValue+j)); Serial.print(" ");
		}
		Serial.println();
	}
	Serial.print("Payload length:\t"); Serial.println(cPacket->payloadLength);
	Serial.print("Payload:\t"); 
	for (int i=0; i< cPacket->payloadLength; i++){
		Serial.print(cPacket->payload[i]); Serial.print(" ");
	}
	Serial.println();
	Serial.println();
}

void sendResponse(CoapPacket *cPacket) {
	udp.beginPacket(udp.remoteIP(), udp.remotePort());

	byte message[4 + cPacket->tokenLength];
	byte x = 1;
	x = x << 2;
	x = x + cPacket->type;
	x = x << 4;
	x = x + cPacket->tokenLength;


	message[0] = x;
	message[1] = cPacket->code;
	message[2] = cPacket->messageID[0];
	message[3] = cPacket->messageID[1];
	Serial.print ("Token ");
	for (int i = 0; i < cPacket->tokenLength; i++)
	{
		message[4 + i] = cPacket->token[i];
		Serial.println(message[4 + i]);
	}
	int len = udp.write(message, sizeof (message));

	udp.endPacket();
}



