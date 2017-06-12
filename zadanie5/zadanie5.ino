#include <SPI.h>
#include <Ethernet.h>
#include <stdlib.h>
#include <EthernetUdp.h>
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

byte mac[] = {
	0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0xFA
};

EthernetUDP udp;

const byte MAX_BUFFER = 255;
byte currentValue = 255;

byte seqNumber = 67;

short localPort = 1244;
const char* wellKnownCore = "</potentiometr>;rt=\"Potentiometr value\";ct=0;if=\"sensor\";,</lamp>;rt=\"Lamp value\";ct=0;</loss>;rt=\"Loss metric\";ct=0;";
byte wellKnownSize = 117;

RF24 radio(7, 8);               // nRF24L01(+) radio attached using Getting Started board
RF24Network network(radio);      // Network uses that radio

const uint16_t this_node = 00;    // Address of our node in Octal format ( 04,031, etc)
const uint16_t other_node = 01;


struct Observer {
	IPAddress address;
	uint16_t port;
	byte tokenLength;
	byte *token;
};

struct payload_t {                 // Structure of our payload
	unsigned short ms;
	unsigned short value;
};


byte packetBuffer[MAX_BUFFER];

unsigned short valueP;

struct Block2Param {
	byte blockNumber;
	byte blockSize;
};

struct Option
{
	uint16_t optionType;
	uint16_t optionLength;
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

enum messageTypes
{
	CON = 0, NON = 1, ACK = 2, RST = 3
};

enum uriPaths
{
	WELL_KNOWN_CORE = 1, POTENTIOMETR = 2, LAMP = 3, LOSS = 4
};

enum acceptedFormats
{
	PLAIN = 0, LINK_FORMAT = 40
};

enum optionTypes
{
	OBSERVE = 6,
	URI_PATH = 11,
	CONTENT_FORMAT = 12,
	ACCEPT = 17,
	BLOCK2 = 23,
	SIZE2 = 28
};

enum messageCodes
{
	EMPTY_MESSAGE,				//0.00
	GET = 1,					//0.01
	PUT = 3,					//0.03
	CREATED = 65,				//2.01
	CHANGED= 68,        		//2.04
	CONTENT = 69,				//2.05
	BAD_REQUEST = 128,			//4.00
	BAD_OPTION = 130, 			//4.02
	NOT_FOUND = 132,			//4.04
	NOT_ALLOWED = 133,			//4.05
	NOT_ACCEPTABLE = 134,		//4.06
	UNSUPPORTED_FORMAT= 143, 	//4.15
	INTERNAL_SERVER_ERROR = 160,//5.00
	SERVICE_UNAVAILABLE = 163 	//5.03

};

Observer observer;
byte observersNumber = 0;
byte lossPacketNumber = 0;
byte RTT = 0;
short latency = 0;

bool areStringsEqual(const char *c1, const char *c2, byte c1Size, byte c2Size)
{
	if (c1Size !=c2Size)
		return false;
	for (int i = 0; i < c1Size; i++)
	{
		if (c1[i] != c2[i])
			return false;
	}
	return true;
}



void setup() {
	IPAddress ip(192, 168, 2, 140);
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


	network.update();
	if (network.available()) {
		RF24NetworkHeader header;
		payload_t payload;
		network.read(header, &payload, sizeof(payload));

		// wyslac do obserwowalnych
		uint16_t receivedValue = payload.value;
		sendToObservers(receivedValue);
	}

}


void receivePacket() {
	// byte packetBuffer[MAX_BUFFER];
	int packetLength = udp.read(packetBuffer, MAX_BUFFER);

	CoapPacket cPacket;

	cPacket.ver = packetBuffer[0] >> 6; // Bity numer 0, 1
	cPacket.type = byte(packetBuffer[0] << 2) / 64; // Bity numer 2, 3
	cPacket.tokenLength = byte(packetBuffer[0] << 4) / 16; // Bity numer 4, 5, 6, 7
	cPacket.code = packetBuffer[1];
	cPacket.messageID[0] = packetBuffer[2];
	cPacket.messageID[1] = packetBuffer[3];


	byte tokenTab[cPacket.tokenLength];
	for (int i = 0; i < cPacket.tokenLength; i++) {
		tokenTab[i] = packetBuffer[4 + i];
	}
	cPacket.token = tokenTab;

	byte optionHeader; // pierwszy bajt opcji
	int optionCounter = 0; // zlicza opcje
	int currentByteNumber = 4 + cPacket.tokenLength;
	byte prevOptionType=0; // potrzebne ĹĽeby dodaÄ‡ do option delta
	bool isNotPayloadByte = true;

	Option options[6]; // zakladam ze jest max 6 opcji
	cPacket.options = options;

	while((currentByteNumber < packetLength) && isNotPayloadByte)
	{
		if (packetBuffer[currentByteNumber] == 255) // bajt samych jedynek rozpoczynajÄ…cy payload
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
			//cPacket.options[optionCounter].optionValue = (byte*) malloc(optLen * sizeof(byte*));

			if (optLen >0) {
				cPacket.options[optionCounter].optionValue = new byte[optLen];
				memcpy(cPacket.options[optionCounter].optionValue, packetBuffer+currentByteNumber, optLen);
			}
			else {
				cPacket.options[optionCounter].optionValue = new byte[1];
				cPacket.options[optionCounter].optionLength = 1;
				cPacket.options[optionCounter].optionValue[0] = 0;
			}
			if (cPacket.options[optionCounter].optionValue == NULL && optLen >0)
				Serial.println("lipa z pamiecia optValue");



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
		//cPacket.payload = (byte*) malloc(cPacket.payloadLength * sizeof(*cPacket.payload));
		cPacket.payload = new byte[cPacket.payloadLength];
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
			{	//free(cPacket.options[i].optionValue);
				delete [] cPacket.options[i].optionValue;
			}
		}
		//free(cPacket.options);
	}
	if (cPacket.payloadLength >0) {
		//free(cPacket.payload);

		delete [] cPacket.payload;
	}


}

void handlePacket(CoapPacket *cPacket)
{
	if (cPacket->type == CON && cPacket->code == EMPTY_MESSAGE) //Ping
	{
		responseForPing(cPacket);
	}
	else if (cPacket->type == NON && cPacket->code ==	GET) 	// GET
	{
		responseForGet(cPacket);
	}
	else if (cPacket->type == NON && cPacket->code ==	PUT) {
		responseForPut(cPacket);
	}
	else if (cPacket->type == ACK) {
		Serial.println("ACK");
	}
	else if (cPacket->type == RST) {
		stopObserving();
	}
	else {
		responseErrorMessage(cPacket, NOT_ALLOWED, "Method not allowed", 18);
	}
}

void responseErrorMessage(CoapPacket *cPacket, byte code, char* payload, byte payloadLength) {
	CoapPacket responsePacket;

	responsePacket.ver = 1;
	responsePacket.type = NON;
	responsePacket.code = code;
	responsePacket.tokenLength = cPacket->tokenLength;
	// responsePacket.token=cPacket.token;
	responsePacket.messageID[0] = cPacket->messageID[0];
	responsePacket.messageID[1] = cPacket->messageID[1];
	responsePacket.token = cPacket->token;

	responsePacket.optionsNumber = 0;
	responsePacket.payloadLength = payloadLength;
	responsePacket.payload = new byte[responsePacket.payloadLength];
	memcpy(responsePacket.payload , payload, responsePacket.payloadLength);

	sendResponse(&responsePacket);

	delete [] responsePacket.payload;
}

void responseForPut(CoapPacket *cPacket) {
	byte uriPathType = 0;
	for(byte i=0; i< cPacket->optionsNumber; i++)
	{
		if (cPacket->options[i].optionType==URI_PATH)
			if (areStringsEqual(cPacket->options[i].optionValue, "lamp",cPacket->options[i].optionLength, 4 )) {
				uriPathType = LAMP;
			}
	}

	if (uriPathType == LAMP) {
		unsigned short lampNewValue = 0;
		bool wrongTypePayloadErrorFlag = false;
		for (byte i=0; i< cPacket->payloadLength; i++) {
			byte digit = cPacket->payload[i] - '0';
			if (digit < 0 || digit > 9) {
				wrongTypePayloadErrorFlag = true;
				break; // wychodzi z fora
			}
			lampNewValue = lampNewValue * 10 + digit;
			if (lampNewValue>1000)
			{
				responseErrorMessage(cPacket, BAD_REQUEST, "Value too big. Max value = 1000", 31);
				return;
			}
		}


		CoapPacket responsePacket;
		responsePacket.optionsNumber = 0;
		responsePacket.payloadLength = 0;


		responsePacket.ver = 1;
		responsePacket.type = NON;
		responsePacket.tokenLength = cPacket->tokenLength;
		responsePacket.messageID[0] = cPacket->messageID[0];
		responsePacket.messageID[1] = cPacket->messageID[1] + 1;
		// TOKEN
		responsePacket.token = cPacket->token;
		bool unsupportedFormatFlag=false;
		short position = getAcceptPosition(cPacket);
		if (position!=-1)
			if (cPacket->options[position].optionLength!=0)
				unsupportedFormatFlag=true;

		if (wrongTypePayloadErrorFlag || unsupportedFormatFlag) {
			
			responsePacket.code = UNSUPPORTED_FORMAT;
			responsePacket.optionsNumber =0;
			responsePacket.payloadLength=23;
			responsePacket.payload = new byte[responsePacket.payloadLength];
			memcpy(responsePacket.payload , "Payload must be number!", responsePacket.payloadLength);
			
		}
		else {
			Serial.print("lamp: ");
			Serial.println(lampNewValue);
			sendPutLampMessage(lampNewValue);
			short answer=receiveValueFromMini();
			if (answer==200)
			{
				responsePacket.code = CHANGED;
				responsePacket.optionsNumber =1;
				Option options[responsePacket.optionsNumber];

				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 0;
				responsePacket.options = options;
				responsePacket.payload= new byte[2];
				responsePacket.payload[0]='O';
				responsePacket.payload[1]='K';
				responsePacket.payloadLength=2;

				delay(1);
			}
			else {
				Serial.println("Wartosc nieustawiona");

				responsePacket.code = SERVICE_UNAVAILABLE;
				responsePacket.optionsNumber =1;
				Option options[responsePacket.optionsNumber];

				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 0;
				responsePacket.options = options;
				responsePacket.payload= new byte[3];
				responsePacket.payload[0]='B';
				responsePacket.payload[1]='A';
				responsePacket.payload[2]='D';
				responsePacket.payloadLength=3;

			}
		}

		sendResponse(&responsePacket);

		if (responsePacket.payloadLength>0)
			delete [] responsePacket.payload;
	}
	else {
		responseErrorMessage(cPacket, NOT_ALLOWED, "Method not allowed", 18);
	}


}

short getAcceptPosition(CoapPacket *cPacket)
{
	for (byte i=0; i < cPacket->optionsNumber; i++)
	{
		if (cPacket->options[i].optionType==ACCEPT)
			return i;

	}
	return -1;
}


void responseForGet(CoapPacket *cPacket)
{
	bool errorFormatPlain = false;
	bool errorFormatLinkF = false;
	bool deleteOptions = false;
	byte uriPathType = 0;
	byte blockSizeIndex = 255;
	bool isSize2Opt = false;
	bool isObserve = false;
	bool isObservable = false;

	CoapPacket responsePacket;
	responsePacket.optionsNumber = 0;
	responsePacket.payloadLength = 0;

	for(byte i=0; i< cPacket->optionsNumber; i++)
	{
		if (cPacket->options[i].optionType==URI_PATH)
		{
			if (areStringsEqual(cPacket->options[i].optionValue, ".well-known",cPacket->options[i].optionLength, 11 )) {
				if ((areStringsEqual(cPacket->options[i+1].optionValue, "core",cPacket->options[i+1].optionLength, 4))) {
					uriPathType = WELL_KNOWN_CORE;
				}
			}
			else if (areStringsEqual(cPacket->options[i].optionValue, "potentiometr",cPacket->options[i].optionLength, 12 )) {
				uriPathType = POTENTIOMETR;
			}
			else if (areStringsEqual(cPacket->options[i].optionValue, "lamp",cPacket->options[i].optionLength, 4 )) {
				uriPathType = LAMP;
			}
			else if (areStringsEqual(cPacket->options[i].optionValue, "loss",cPacket->options[i].optionLength, 4 )) {
				uriPathType = LOSS;
			}

		}
		else if (cPacket->options[i].optionType==CONTENT_FORMAT)
		{
			// Czy to wgl bedzie???????????????????????????????????????????????

		}
		else if (cPacket->options[i].optionType==ACCEPT)
		{
			if (cPacket->options[i].optionValue[0] != PLAIN && uriPathType != WELL_KNOWN_CORE)
			{
				errorFormatPlain = true;
			}
			else if (cPacket->options[i].optionValue[0] != LINK_FORMAT && uriPathType == WELL_KNOWN_CORE){
				errorFormatLinkF = true;
			}

		}
		else if (cPacket->options[i].optionType==BLOCK2)
		{
			blockSizeIndex = i;
		}
		else if (cPacket->options[i].optionType==SIZE2)
		{
			if (cPacket->options[i].optionValue[0] == 0) {
				isSize2Opt = true;
			}
		}
		else if (cPacket->options[i].optionType==OBSERVE)
		{
			if (cPacket->options[i].optionValue[0] == 0) {
				isObserve = true;
				Serial.println("Observe");
			}
			else if (cPacket->options[i].optionValue[0] == 1) {
				Serial.println("Cancel Observe");
				stopObserving();
			}
			isObservable = true;
		}
		else if (cPacket->options[i].optionType%2 == 1){
			Serial.println("krytyczna");
			responseErrorMessage(cPacket, BAD_OPTION, "Option not supported!", 21);
			return;
		}
	}

	// HEADER
	responsePacket.ver = 1;
	responsePacket.type = NON;
	responsePacket.tokenLength = cPacket->tokenLength;
	responsePacket.messageID[0] = cPacket->messageID[0];
	responsePacket.messageID[1] = cPacket->messageID[1] + 1;
	// TOKEN
	responsePacket.token = cPacket->token;

	if (!errorFormatPlain && !errorFormatLinkF)
	{
		responsePacket.code = CONTENT; //2.05

		if (uriPathType == WELL_KNOWN_CORE)
		{
			if (isObservable) {
				responseErrorMessage(cPacket, BAD_OPTION, "Option Observe not supported here", 33);
				return;
			}
			else {
				//responsePacket.type = CON;

				if (isSize2Opt) {
					responsePacket.optionsNumber = 3;
				} else {
					responsePacket.optionsNumber = 2;
				}

				//Option options[responsePacket.optionsNumber];
				deleteOptions = true;
				Option* options = new Option[responsePacket.optionsNumber];

				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 1;
				options[0].optionValue = new byte[options[0].optionLength];
				options[0].optionValue[0] = LINK_FORMAT; //core link format

				options[1].optionType = BLOCK2;
				options[1].optionLength = 1;
				options[1].optionValue = new byte[options[1].optionLength];

				if (isSize2Opt) {
					options[2].optionType = SIZE2;
					options[2].optionLength = 1;
					options[2].optionValue = new byte[1];
					options[2].optionValue[0] = wellKnownSize;
				}


				responsePacket.options = options;

				if (blockSizeIndex != 255) { // nie jest to pierwszy blok
					delay(1);
					Block2Param block = parseBlock2(cPacket->options[blockSizeIndex]);

					bool isNextBlock = false;
					if(wellKnownSize > (block.blockNumber+1) * power(2,4+block.blockSize)) { // czy bedzie jeszcze jeden blok
						isNextBlock = true;
					}

					options[1].optionValue[0] = block.blockNumber ;
					options[1].optionValue[0] = options[1].optionValue[0] << 1;
					if (isNextBlock) {
						options[1].optionValue[0] = options[1].optionValue[0] + 1; // bedzie nastepny blok
					}
					options[1].optionValue[0] = options[1].optionValue[0] << 3;
					options[1].optionValue[0] = options[1].optionValue[0] + block.blockSize; // 32 bajtowe bloki

					responsePacket.payloadLength = power(2,4+block.blockSize);

					if (!isNextBlock) {
						responsePacket.payloadLength = wellKnownSize - (block.blockNumber) * power(2,4+block.blockSize);
					}

					responsePacket.payload = new char[responsePacket.payloadLength];
					byte plus =(block.blockNumber) * power(2,4+block.blockSize);
					if (block.blockNumber != 0) {
						//plus++;
					}
					memcpy(responsePacket.payload , wellKnownCore + plus, responsePacket.payloadLength);
					delay(1);
				}
				else {	// wysylanie pierwszego bloku
					delay(1);
					options[1].optionValue[0] = 1;	// bedzie nastepny blok
					options[1].optionValue[0] = options[1].optionValue[0] << 3;
					options[1].optionValue[0] = options[1].optionValue[0] + 1; // 32 bajtowe bloki

					responsePacket.payload = new char[32];
					memcpy(responsePacket.payload, wellKnownCore, 32);
					responsePacket.payloadLength = 32;

					delay(1);
				}

				Serial.println("BLOCK");
			}
		}
		else if (uriPathType == POTENTIOMETR)
		{
			if (isObserve) {
				registerObserver(cPacket);
			}

			sendGetValueMessage(1);
			uint16_t receiveValue=receiveValueFromMini();
			Serial.print("Potenc: ");
			Serial.println(receiveValue);

			responsePacket.optionsNumber =1;
			if (isObserve) {
				responsePacket.optionsNumber = 2;
			}

			//Option options[responsePacket.optionsNumber];
			Option* options = new Option[responsePacket.optionsNumber];
			deleteOptions = true;


			if (isObserve) {
				options[0].optionType = OBSERVE;
				options[0].optionLength = 1;
				options[0].optionValue = new byte[options[0].optionLength];
				options[0].optionValue[0] = 66;

				options[1].optionType = CONTENT_FORMAT;
				options[1].optionLength = 0;
			} else {
				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 0;
			}

			responsePacket.options = options;

			if (receiveValue >= 1000)
				responsePacket.payloadLength = 4;
			else if (receiveValue >= 100)
				responsePacket.payloadLength = 3;
			else if (receiveValue >= 10)
				responsePacket.payloadLength = 2;
			else
				responsePacket.payloadLength = 1;

			Serial.println("new payload 1");
			byte* payload = new byte[responsePacket.payloadLength];
			uint16_t prevValue = 0;
			for (byte i=0; i<responsePacket.payloadLength; i++ ) { // konwersja payloadu
				payload[i] = receiveValue / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
				prevValue = (prevValue + payload[i]) * 10;
				payload[i] += '0';
			}

			responsePacket.payload = payload;
		}
		else if (uriPathType == LAMP)
		{
			if (isObservable) {
				responseErrorMessage(cPacket, BAD_OPTION, "Option Observe not supported here", 33);
				return;
			}
			else {
				sendGetValueMessage(2);
				uint16_t receiveValue=receiveValueFromMini();
				Serial.println("lamp");
				Serial.println(receiveValue);

				responsePacket.optionsNumber =1;
				Option options[responsePacket.optionsNumber];

				responsePacket.optionsNumber =1;
				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 0;

				responsePacket.options = options;

//			if (lampValue >= 1000)
//				responsePacket.payloadLength = 4;
//			else if (lampValue >= 100)
//				responsePacket.payloadLength = 3;
//			else if (lampValue >= 10)
//				responsePacket.payloadLength = 2;
//			else
//				responsePacket.payloadLength = 1;
//
//			byte payload[responsePacket.payloadLength];
//			uint16_t prevValue = 0;
//			for (byte i=0; i<responsePacket.payloadLength; i++ ) {
//				payload[i] = lampValue / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
//				prevValue = (prevValue + payload[i]) * 10;
//				payload[i] += '0';
//			}
				if (receiveValue >= 1000)
					responsePacket.payloadLength = 4;
				else if (receiveValue >= 100)
					responsePacket.payloadLength = 3;
				else if (receiveValue >= 10)
					responsePacket.payloadLength = 2;
				else
					responsePacket.payloadLength = 1;

				//  byte* payload = (byte*) malloc(responsePacket.payloadLength * sizeof(byte));
				Serial.println("new payload 1");
				byte* payload = new byte[responsePacket.payloadLength];
				uint16_t prevValue = 0;
				for (byte i=0; i<responsePacket.payloadLength; i++ ) {
					payload[i] = receiveValue / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
					prevValue = (prevValue + payload[i]) * 10;
					payload[i] += '0';
				}

				responsePacket.payload = payload;
			}
		}
		else if (uriPathType == LOSS)
		{
			if (isObservable) {
				responseErrorMessage(cPacket, BAD_OPTION, "Option Observe not supported here", 33);
				return;
			}
			else {
				if(RTT != 0)
				{
					latency += RTT;
				}
				if (isSize2Opt) {
					responsePacket.optionsNumber = 3;
				} else {
					responsePacket.optionsNumber = 2;
				}

				deleteOptions = true;
				Option* options = new Option[responsePacket.optionsNumber];

				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 1;
				options[0].optionValue = new byte[options[0].optionLength];
				options[0].optionValue[0] = LINK_FORMAT; //core link format

				options[1].optionType = BLOCK2;
				options[1].optionLength = 1;
				options[1].optionValue = new byte[options[1].optionLength];

				if (isSize2Opt) {
					options[2].optionType = SIZE2;
					options[2].optionLength = 1;
					options[2].optionValue = new byte[1];
					options[2].optionValue[0] = 1;
				}

				responsePacket.options = options;

				Block2Param block = parseBlock2(cPacket->options[blockSizeIndex]);

				bool isNextBlock = true;
				byte blockSize = 32;

				options[1].optionValue[0] = 1;  // bedzie nastepny blok
				if (lossPacketNumber == 99)
				{
					isNextBlock = false;
					options[1].optionValue[0] = 0;  // nie ma kolejnego bloku
					//wyswietl wynik
					Serial.print("Average RTT (100 packets): ");
					Serial.println(latency/100);
					lossPacketNumber = 0;
					latency = 0;
				}

				options[1].optionValue[0] = options[1].optionValue[0] << 3;
				options[1].optionValue[0] = options[1].optionValue[0] + 1; // 32 bajtowe bloki

				responsePacket.payload = new char[1];
				responsePacket.payloadLength = 1;

				memcpy(responsePacket.payload , "1", responsePacket.payloadLength);
				lossPacketNumber +=1;
				delay(1);
				RTT = millis();
			}
		}
		else {
			responsePacket.code = NOT_FOUND; //4.04
			responsePacket.payloadLength = 14;
			responsePacket.payload = new byte[responsePacket.payloadLength];
			memcpy(responsePacket.payload , "Wrong URI-PATH", responsePacket.payloadLength);
		}
	}
	else
	{
		responsePacket.code = NOT_ACCEPTABLE; 
		if (errorFormatPlain){
			responsePacket.payloadLength = 22;
			responsePacket.payload = new byte[responsePacket.payloadLength];
			memcpy(responsePacket.payload , "Accepted format: plain", responsePacket.payloadLength);
			}
		else if (errorFormatLinkF){
			responsePacket.payloadLength = 28;
			responsePacket.payload = new byte[responsePacket.payloadLength];
			memcpy(responsePacket.payload , "Accepted format: link-format", responsePacket.payloadLength);

		}
		
	}
	//printCoapPacket(&responsePacket);
	sendResponse(&responsePacket);

	// Zwolnienie pamieci z optionValue
	for (byte i=0; i<responsePacket.optionsNumber; i++ ) {
		if (responsePacket.options[i].optionLength > 0) {
			delete [] responsePacket.options[i].optionValue;
		}
	}
	if (deleteOptions) {
		delete [] responsePacket.options;
	}
	if (responsePacket.payloadLength >0) {
		delete [] responsePacket.payload;
	}
}

void registerObserver(CoapPacket *cPacket) {
	if (observersNumber < 1) {
		Serial.println("Register observer");

		observer.address = udp.remoteIP();
		observer.port = udp.remotePort();
		observer.tokenLength = cPacket->tokenLength;
		observer.token = new byte[observer.tokenLength];
		memcpy(observer.token, cPacket->token, observer.tokenLength);

		// Wyslac do mini
		payload_t payload = { 4, 0};
		RF24NetworkHeader header(/*to node*/ other_node);
		bool ok = network.write(header, &payload, sizeof(payload));

		observersNumber++;
	}
}

void sendToObservers(uint16_t receiveValue) {
	if (observersNumber > 0){
		CoapPacket responsePacket;
		responsePacket.optionsNumber = 0;
		responsePacket.payloadLength = 0;
	
		// HEADER
		responsePacket.ver = 1;
		responsePacket.type = NON;
		responsePacket.code = CONTENT;
		responsePacket.messageID[0] = 100;
		responsePacket.messageID[1] = seqNumber;
		responsePacket.payloadLength=0;
	
		responsePacket.optionsNumber = 2;
		Option options[responsePacket.optionsNumber];
		options[0].optionType = OBSERVE;
		options[0].optionLength = 1;
		options[0].optionValue = new byte[options[0].optionLength];
		options[0].optionValue[0] = seqNumber++; // zmienic!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	
		options[1].optionType = CONTENT_FORMAT;
		options[1].optionLength = 0;
	
		responsePacket.options = options;
	
		if (receiveValue >= 1000)
			responsePacket.payloadLength = 4;
		else if (receiveValue >= 100)
			responsePacket.payloadLength = 3;
		else if (receiveValue >= 10)
			responsePacket.payloadLength = 2;
		else
			responsePacket.payloadLength = 1;
	
		byte* payload = new byte[responsePacket.payloadLength];
		uint16_t prevValue = 0;
		for (byte i=0; i<responsePacket.payloadLength; i++ ) { // konwersja payloadu
			payload[i] = receiveValue / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
			prevValue = (prevValue + payload[i]) * 10;
			payload[i] += '0';
		}
	
		responsePacket.payload = payload;
		Serial.print("payload: ");
		for (byte i=0; i< responsePacket.payloadLength; i++) {
			Serial.print(char(responsePacket.payload[i]));
		}
		Serial.println();
	
		Serial.println("wysylanie");
		responsePacket.tokenLength = observer.tokenLength;
		responsePacket.token = observer.token;
		sendResponse(&responsePacket, observer.address, observer.port);
	
	
	
		// Zwolnienie pamieci z optionValue
		for (byte i=0; i<responsePacket.optionsNumber; i++ ) {
			if (responsePacket.options[i].optionLength > 0) {
				delete [] responsePacket.options[i].optionValue;
			}
		}
	
		if (responsePacket.payloadLength >0) {
			delete [] responsePacket.payload;
		}
	}
}

Block2Param parseBlock2(Option option) {
	Block2Param block;
	block.blockNumber = option.optionValue[0] >> 4;
	block.blockSize = byte(option.optionValue[0] << 5) >> 5;
	return block;
}

void stopObserving() {
	// Usunac z listy
	if (observersNumber > 0) {
		observersNumber = 0;
	}
	// wyslac do mini
	payload_t payload = { 6, 0};
	RF24NetworkHeader header(other_node);
	bool ok = network.write(header, &payload, sizeof(payload));
}

void responseForPing(CoapPacket *cPacket)
{
	CoapPacket responsePacket;

	responsePacket.ver = 1;
	responsePacket.type = ACK;
	responsePacket.code = (byte)0;
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
	sendGetValueMessage(1);
	currentValue = receiveValueFromMini();
	Serial.println(currentValue);
	return currentValue;
}


bool sendGetValueMessage(byte type) {

	network.update();

	payload_t payload = { type, 0};
	RF24NetworkHeader header(/*to node*/ other_node);
	bool ok = network.write(header, &payload, sizeof(payload));

	return ok;

}

unsigned short receiveValueFromMini() {
	while(true)
	{
		network.update();

		if (network.available()) {
			RF24NetworkHeader header;
			payload_t payload;
			network.read(header, &payload, sizeof(payload));
			return payload.value;
		}
	}
	return 2000;
}


bool sendPutLampMessage(unsigned short value)
{
	network.update();

	payload_t payload = {3,value};
	RF24NetworkHeader header(/*to node*/ other_node);
	bool ok = network.write(header, &payload, sizeof(payload));

	return ok;


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

void sendResponse(CoapPacket *cPacket, IPAddress address, uint16_t port) {
	udp.beginPacket(address, port);

	int packetSize = calculateCoapPacketSize(cPacket);
//	Serial.print("Packet size: ");
//	Serial.println(packetSize);
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

			if (optionTypeMinusDelta >= 269) { // 2 bytes extension type
				optionHeader = 14;
				message[currentByteNumber++] = byte((optionTypeMinusDelta - 14) >> 8);
				message[currentByteNumber++] = byte(optionTypeMinusDelta - 14);
			}
			else if (optionTypeMinusDelta >= 13) { // 1 byte extension type
				optionHeader = 13;
				message[currentByteNumber++] = byte(optionTypeMinusDelta - 13);
			}
			else { // no extension type
				optionHeader = optionTypeMinusDelta;
			}
			optionHeader = optionHeader << 4;

			if (cPacket->options[i].optionLength >= 269) { // 2 bytes extension length
				optionHeader += 14;
				message[currentByteNumber++] = byte((cPacket->options[i].optionLength - 14) >> 8);
				message[currentByteNumber++] = byte(cPacket->options[i].optionLength - 14);
			}
			else if (cPacket->options[i].optionLength >= 13) { // 1 byte extension length
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
	if (cPacket->payloadLength > 0) {
		message[currentByteNumber++] = 255; // bajt jedynek
		memcpy(message+currentByteNumber, cPacket->payload, cPacket->payloadLength);
	}

	int len = udp.write(message, sizeof (message));
	udp.endPacket();
}
void sendResponse(CoapPacket *cPacket) {
	sendResponse(cPacket, udp.remoteIP(), udp.remotePort());
	//sendToObservers(100);
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

	if (cPacket->payloadLength > 0) { // jeĹ›li tak to payload istnieje
		size++; // bajt samych jedynek
		size += cPacket->payloadLength;
	}

	return size;
}

uint16_t power(byte base, byte number) {
	uint16_t value = 1;
	for (byte i=0; i< number; i++) {
		value = value * base;
	}
	return value;
}


