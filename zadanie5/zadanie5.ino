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

struct CoapPackage
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
	byte payload;
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


	CoapPackage cPackage;

	cPackage.ver = packetBuffer[0] >> 6; // Bity numer 0, 1
	cPackage.type = byte(packetBuffer[0] << 2) / 64; // Bity numer 2, 3
	cPackage.tokenLength = byte(packetBuffer[0] << 4) / 16; // Bity numer 4, 5, 6, 7
	cPackage.code = packetBuffer[1];
	cPackage.messageID[0] = packetBuffer[2];
	cPackage.messageID[1] = packetBuffer[3];

	if ( cPackage.ver != 1){
		Serial.println("Zły format nagłówka");
		return;
	}

	byte tokenTab[cPackage.tokenLength];
	for (int i = 0; i < cPackage.tokenLength; i++){
		tokenTab[i] = packetBuffer[4 + i];
	}
	cPackage.token = tokenTab;
	

	//int bytesLeft=len-4-cPackage.tokenLength;
	
	byte optionHeader; // pierwszy bajt opcji
	int optionCounter = 0; // zlicza opcje
	int currentByteNumber = 4 + cPackage.tokenLength;
	byte prevOptionType=0; // potrzebne żeby dodać do option delta
	Option* options;

	// Response with options
	CoapResponse coapResponse;

	
	bool isNotPayloadByte = true;
	
	while((currentByteNumber < packetLength) && isNotPayloadByte)
	{
		Serial.println();
		if (packetBuffer[currentByteNumber] == 255) // bajt samych jedynek rozpoczynający payload
		{
			isNotPayloadByte = false;
		}
		else {		
			options = realloc(options, (optionCounter+1)*sizeof(Option));
			optionHeader = packetBuffer[currentByteNumber++];
			
			byte optType = byte(optionHeader >> 4);
			byte optLen = byte(optionHeader << 4) / 16;

			if (optType > 12) // Option delta
			{
				if (optType - 12 == 2){ // Type extension 2 bytes
					optType += packetBuffer[currentByteNumber++];
					optType += packetBuffer[currentByteNumber++];
				}
				else{ // Type extension 1 byte
					optType += packetBuffer[currentByteNumber++];
				}
			}

			if (optLen > 12) // Option length
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
			prevOptionType += optType;
			currentByteNumber += optLen;
			optionCounter++;
		}
	}
	
	// KONIEC WCZYTYWANIA OPCJI
	cPackage.optionsNumber = optionCounter;
	cPackage.options = options;
	

	if (packetBuffer[currentByteNumber] == 255){ 	// Wczytywanie payloadu
		
	}

	printCoapPackage(&cPackage);

	if (cPackage.type == 0 && cPackage.code == 0) //Ping
	{
		Serial.println("ping");
		responseForPing(cPackage);
	}
	else if (cPackage.type == 1 && cPackage.code == 1)
	{
		Serial.println("GET");
		responseForGet(&cPackage);
	}

	for (int i=0; i<optionCounter; i++)
		free(options[i].optionValue);
	free(options);
}

void responseForGet(CoapPackage *cPackage)
{
	CoapPackage responseHeader;
	CoapPackage local = *cPackage;
	responseHeader.ver = 1;
	responseHeader.type = 1;
	responseHeader.code = (byte)69; //2.05
	responseHeader.tokenLength = cPackage->tokenLength;
	// responseHeader.token=cPackage->token;
	responseHeader.token = cPackage->token;
	Serial.print("Token ");
	Serial.println(*responseHeader.token);

	responseHeader.messageID[0] = cPackage->messageID[0];
	responseHeader.messageID[1] = cPackage->messageID[1] + 1;

	sendResponse(&responseHeader);


}


void responseForPing(CoapPackage cPackage)
{
	CoapPackage responseHeader;

	responseHeader.ver = 1;
	responseHeader.type = 2;
	responseHeader.code = (byte)64;
	responseHeader.tokenLength = cPackage.tokenLength;
	// responseHeader.token=cPackage.token;
	responseHeader.messageID[0] = cPackage.messageID[0];
	responseHeader.messageID[1] = cPackage.messageID[1];

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

void printCoapPackage(CoapPackage *cPackage){
	Serial.print("Ver:\t"); Serial.println(cPackage->ver);
	Serial.print("Type:\t"); Serial.println(cPackage->type);
	Serial.print("TKL:\t"); Serial.println(cPackage->tokenLength);
	Serial.print("Code:\t"); Serial.println(cPackage->code);
	Serial.print("Token:\t0x");
	for (int i=0; i< cPackage->tokenLength; i++){
		Serial.print(cPackage->token[i], HEX);
	}
	Serial.println();
	Serial.print("Liczba opcji:\t");	Serial.println(cPackage->optionsNumber);
	for (int i=0; i< cPackage->optionsNumber; i++)
	{
		Serial.print("Opcja nr: "); Serial.print(i);
		Serial.print("\tDlugosc opcji: ");	Serial.print(cPackage->options[i].optionLength);
		Serial.print("\tNr opcji: ");	Serial.print(cPackage->options[i].optionType);
		Serial.print("\tOption value: "); 
		for (int j=0; j<cPackage->options[i].optionLength; j++){
			Serial.print(*(cPackage->options[i].optionValue+j)); Serial.print(" ");
		}
		Serial.println();
	}
	
	

}

void sendResponse(CoapPackage *cPackage) {
	udp.beginPacket(udp.remoteIP(), udp.remotePort());

	byte message[4 + cPackage->tokenLength];
	byte x = 1;
	x = x << 2;
	x = x + cPackage->type;
	x = x << 4;
	x = x + cPackage->tokenLength;


	message[0] = x;
	message[1] = cPackage->code;
	message[2] = cPackage->messageID[0];
	message[3] = cPackage->messageID[1];
	Serial.print ("Token ");
	for (int i = 0; i < cPackage->tokenLength; i++)
	{
		message[4 + i] = cPackage->token[i];
		Serial.println(message[4 + i]);
	}
	int len = udp.write(message, sizeof (message));

	udp.endPacket();
}



