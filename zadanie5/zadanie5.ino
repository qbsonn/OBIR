#include <SPI.h>
#include <Ethernet.h>
#include <stdlib.h>
#include <EthernetUdp.h>
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

#include "enums.h"
#include "structures.h"
#include "simpleOperations.h"
#include "methodsToMini.h"

byte mac[] = {
	0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0xFA
};

EthernetUDP udp;
short localPort = 1244;

//const byte MAX_BUFFER = 255; // wielkosc bufora
//byte packetBuffer[MAX_BUFFER];

byte bufferSize;
byte* packetBuffer;

byte seqNumber = 67; // numer sekwencji do observe

const char* wellKnownCore = "</potentiometr>;rt=\"Potentiometr value\";ct=0;if=\"sensor\";,</lamp>;rt=\"Lamp value\";ct=0;</loss>;rt=\"Loss metric\";ct=0;</delay>;rt=\"Delay metric\";ct=0;</delay_variation>;rt=\"Delay varation metric\";ct=0;";
byte wellKnownSize = 200;

const uint16_t this_node = 00;    // identyfikator tego wezla (Uno)

Observer observer;
byte observersNumber = 0;	// liczba obserwatorow, zaimplementowana obsluga tylko jedengo obserwatora

NotAckPacket notAackPacket;	// wiadomosci do potwierdzenia, w razie koniecznosci retransmitowana 
uint16_t retransmitTime = 1000;
unsigned long prevRetransmitTime;
bool isMessageConSent = false;
byte retransmitCounter = 0;

byte lossPacketNumber = 0;
byte RTT = 0;
short latency = 0;
uint16_t lossMetric = 0;
unsigned short lastRTTTime = 0;
unsigned short max_delay = 0;
unsigned short min_delay = 1000;

void setup() {
	IPAddress ip(192, 168, 2, 140);
	Serial.begin(115200);
	//if (Ethernet.begin(mac) == 0){
		Ethernet.begin(mac, ip);
	//}
	Serial.println(Ethernet.localIP());
	udp.begin(localPort);

	//czesc radiowa
	SPI.begin();
	radio.begin();
	network.begin(30, this_node); // ustalenie kanalu komunikacji radiowej i identyfikatora swojego wezla
	
	Serial.println("Setup finished");
}

void loop() {
	int packetSize = udp.parsePacket(); // sprawdzenie czy odebrano pakiet
	if (packetSize) { 
		bufferSize = packetSize;
		receivePacket();	// obsluga pakietu
		
	}


	network.update();
	if (network.available()) {	// sprawdzenie czy jest jakas wiadomosc od mini
		uint16_t receivedValue = receiveObsValue();
		sendToObservers(receivedValue);
	}

	if (isMessageConSent){
		if (millis() - prevRetransmitTime > retransmitTime){
			if (retransmitCounter < 4){
				lossMetric++;
				retransmit();
			}
			else{
				lossMetric++;
				isMessageConSent = false;
				retransmitCounter = 0;
				if (notAackPacket.tokenLength > 0){
					delete [] notAackPacket.token;
				}
				//strata pakietu
			}
		}
	}

}

void receivePacket() {
	packetBuffer = new byte[bufferSize];
	int packetLength = udp.read(packetBuffer, bufferSize);
//	int packetLength = udp.read(packetBuffer, MAX_BUFFER);	// zapisanie pakietu do bufora

	// PARSOWANIE WIADOMOSCI
	CoapPacket cPacket;

	cPacket.ver = packetBuffer[0] >> 6; // Bity numer 0, 1
	cPacket.type = byte(packetBuffer[0] << 2) / 64; // Bity numer 2, 3
	cPacket.tokenLength = byte(packetBuffer[0] << 4) / 16; // Bity numer 4, 5, 6, 7
	cPacket.code = packetBuffer[1];
	cPacket.messageID[0] = packetBuffer[2];
	cPacket.messageID[1] = packetBuffer[3];

	byte tokenTab[cPacket.tokenLength];
	for (byte i = 0; i < cPacket.tokenLength; i++) {
		tokenTab[i] = packetBuffer[4 + i];
	}
	cPacket.token = tokenTab;

	byte optionHeader; // pierwszy bajt opcji
	byte optionCounter = 0; // licznik opcji w pakiecie
	byte currentByteNumber = 4 + cPacket.tokenLength;
	byte prevOptionType=0; // potrzebne zeby dodac do option delta
	bool isNotPayloadByte = true;

	Option options[6]; // zakladam ze jest max 6 opcji
	cPacket.options = options;

	while((currentByteNumber < packetLength) && isNotPayloadByte)
	{
		if (packetBuffer[currentByteNumber] == 255) // bajt samych jedynek rozpoczynajacy payload
		{
			isNotPayloadByte = false;
		}
		else {
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

			if (optLen >0) {
				cPacket.options[optionCounter].optionValue = new byte[optLen];
				memcpy(cPacket.options[optionCounter].optionValue, packetBuffer+currentByteNumber, optLen);
			}
			else { // jesli opcja nie ma value - np content format: plain 
				cPacket.options[optionCounter].optionValue = new byte[1];
				cPacket.options[optionCounter].optionLength = 1;
				cPacket.options[optionCounter].optionValue[0] = 0;
			}

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
		cPacket.payload = new byte[cPacket.payloadLength];
		memcpy(cPacket.payload, packetBuffer+currentByteNumber, cPacket.payloadLength);
	}
	else {
		cPacket.payloadLength = 0;
	}
	// KONIEC PARSOWANIA PAKIETU

	delete [] packetBuffer;	// zwolnienie pamieci bufora
	
	handlePacket(&cPacket);	// obsluga pakietu

	// Zwolnienie pamieci po obsludze pakietu
	if (cPacket.optionsNumber > 0) {
		for (byte i=0; i<cPacket.optionsNumber; i++) {
			if (cPacket.options[i].optionLength > 0){	
				delete [] cPacket.options[i].optionValue;
			}
		}
	}
	if (cPacket.payloadLength >0) {
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
		handleACK(cPacket);
	}
	else if (cPacket->type == RST) {
		stopObserving();
	}
	else {
		responseErrorMessage(cPacket,NON, NOT_ALLOWED, "Method not allowed", 18);
	}
}

void handleACK(CoapPacket *cPacket){
	if (cPacket->messageID[0] == notAackPacket.messageID[0] && cPacket->messageID[1] == notAackPacket.messageID[1]){
		Serial.println("ACK potwierdzone");
		isMessageConSent = false;
		retransmitCounter = 0;
		if (notAackPacket.tokenLength > 0){
			delete [] notAackPacket.token;
		}
	}
}

void responseErrorMessage(CoapPacket *cPacket, byte type ,byte code, char* payload, byte payloadLength) {
	CoapPacket responsePacket;

	responsePacket.ver = 1;
	responsePacket.type = type;
	responsePacket.code = code;
	responsePacket.tokenLength = cPacket->tokenLength;
	responsePacket.messageID[0] = cPacket->messageID[0];
	responsePacket.messageID[1] = cPacket->messageID[1];
	responsePacket.token = cPacket->token;

	responsePacket.optionsNumber = 1;
	Option options[1];
	options[0].optionType = CONTENT_FORMAT;
	options[0].optionLength = 0;
	responsePacket.options = options;
				
	responsePacket.payloadLength = payloadLength;
	responsePacket.payload = new byte[responsePacket.payloadLength];
	memcpy(responsePacket.payload , payload, responsePacket.payloadLength);

	sendResponse(&responsePacket);

	delete [] responsePacket.payload;
}

void responseForPut(CoapPacket *cPacket) {
	byte uriPathType = 0;	// okresla zasob na ktorym chcemy zrobic put
	
	for(byte i=0; i< cPacket->optionsNumber; i++)	// sprawdzanie czy jest opcja URI-PATH i na co wskazuje
	{
		if (cPacket->options[i].optionType==URI_PATH)
			if (areStringsEqual(cPacket->options[i].optionValue, "lamp",cPacket->options[i].optionLength, 4 )) {
				uriPathType = LAMP;
			}
	}

	if (uriPathType == LAMP) {
		unsigned short lampNewValue = 0;
		bool wrongTypePayloadErrorFlag = false;

		if (cPacket->payloadLength == 0){
			responseErrorMessage(cPacket,NON,  BAD_REQUEST, "Give specific value", 19);
			return;
		}
		for (byte i=0; i< cPacket->payloadLength; i++) {
			byte digit = cPacket->payload[i] - '0';
			if (digit < 0 || digit > 9) {
				wrongTypePayloadErrorFlag = true;
				break; // wychodzi z fora
			}
			lampNewValue = lampNewValue * 10 + digit;
			if (lampNewValue>1000)
			{
				responseErrorMessage(cPacket,NON,  BAD_REQUEST, "Value too big. Max value = 1000", 31);
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
		responsePacket.token = cPacket->token;
		
		bool unsupportedFormatFlag=false;
		short position = getAcceptPosition(cPacket);	// sprawdza indeks opcji accepted
		if (position!=-1)	// jesli opcja accepted jest w sparsowanym pakiecie
			if (cPacket->options[position].optionLength!=0)	// jesli opcja jest rozna od text/plain
				unsupportedFormatFlag=true;

		if (wrongTypePayloadErrorFlag || unsupportedFormatFlag) {
			
			responsePacket.code = UNSUPPORTED_FORMAT;
			responsePacket.optionsNumber =0;
			responsePacket.payloadLength=23;
			responsePacket.payload = new byte[responsePacket.payloadLength];
			memcpy(responsePacket.payload , "Payload must be number!", responsePacket.payloadLength);
			
		}
		else {
			
			bool ok = putLampValue(lampNewValue);	// wyslanie do Mini wiadomosci
			if (ok)
			{
				responsePacket.code = CHANGED;
				responsePacket.optionsNumber =1;
				Option options[responsePacket.optionsNumber];

				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 0;
				responsePacket.options = options;
				responsePacket.payloadLength=2;
				responsePacket.payload = new byte[responsePacket.payloadLength];
				memcpy(responsePacket.payload , "OK", responsePacket.payloadLength);

				delay(1);
			}
			else {
				responseErrorMessage(cPacket, NON, SERVICE_UNAVAILABLE, "Cannot connect to mini", 22);
				return;
			}
		}

		sendResponse(&responsePacket);

		if (responsePacket.payloadLength>0)
			delete [] responsePacket.payload;
	}
	else {
		responseErrorMessage(cPacket,NON, NOT_ALLOWED, "Method not allowed", 18);
	}
}

// zwraca index opcji accept w elemencie options struktury CoapPacket
short getAcceptPosition(CoapPacket *cPacket){
	for (byte i=0; i < cPacket->optionsNumber; i++){
		if (cPacket->options[i].optionType==ACCEPT)
			return i;
	}
	return -1;
}


void responseForGet(CoapPacket *cPacket)
{
	bool errorFormatPlain = false;	// zadanie ma nieobslugiwana wartosc opcji accepted (inny format niz link-format dla well/known-core)
	bool errorFormatLinkF = false;	// zadanie ma nieobslugiwana wartosc opcji accepted (inny format niz plain dla reszty zasobow)
	bool deleteOptions = false;	// czy trzeba robic delete[] na opcjach - dynamiczne przydzielanie opcji
	byte uriPathType = 0;	// zmienna okreslajaca zadany zasob 
	byte blockSizeIndex = 255;	// index opcji block2 w elemencie options struktury CoapPacket, jesli == 255 to znaczy ze nie ma tej opcji w pakiecie 
	bool isSize2Opt = false;	// czy opcja Size2 jest uwzgledniona
	bool isObserve = false;	// czy jest opcja Observe == 0 (zadanie obserwowania)
	bool isObservable = false;	// czy jest opcja Observe (obojetnie jaka wartosc)

	CoapPacket responsePacket;
	responsePacket.optionsNumber = 0;
	responsePacket.payloadLength = 0;

	for(byte i=0; i< cPacket->optionsNumber; i++)	// Sprawdzenie opcji i ustawienie odpowiednich flag/zmiennych
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
      else if (areStringsEqual(cPacket->options[i].optionValue, "delay",cPacket->options[i].optionLength, 5 )) {
        uriPathType = DELAY;
      }
      else if (areStringsEqual(cPacket->options[i].optionValue, "delay_variation",cPacket->options[i].optionLength, 15)) {
        uriPathType = DELAY_VARIATION;
      }
		}
		else if (cPacket->options[i].optionType==ACCEPT){
			if (cPacket->options[i].optionValue[0] != PLAIN && uriPathType != WELL_KNOWN_CORE){
				errorFormatPlain = true;
			}
			else if (cPacket->options[i].optionValue[0] != LINK_FORMAT && uriPathType == WELL_KNOWN_CORE){
				errorFormatLinkF = true;
			}
		}
		else if (cPacket->options[i].optionType==BLOCK2){
			blockSizeIndex = i;
		}
		else if (cPacket->options[i].optionType==SIZE2){
			if (cPacket->options[i].optionValue[0] == 0) {
				isSize2Opt = true;
			}
		}
		else if (cPacket->options[i].optionType==OBSERVE){
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
		else if (cPacket->options[i].optionType%2 == 1){	// opcja "krytyczna", ktorej nie obslugujemy
			responseErrorMessage(cPacket, RST, BAD_OPTION, "Option not supported!", 21);
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
				responseErrorMessage(cPacket, NON, BAD_OPTION, "Option Observe not supported here", 33);
				return;
			}
			else {
				responsePacket.type = CON;

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
					options[2].optionValue[0] = wellKnownSize;
				}

				responsePacket.options = options;

				if (notAackPacket.tokenLength > 0 && isMessageConSent){
					delete [] notAackPacket.token;
				}
				notAackPacket.messageID[0] = responsePacket.messageID[0];
				notAackPacket.messageID[1] = responsePacket.messageID[1];
				notAackPacket.tokenLength = responsePacket.tokenLength;
				notAackPacket.token = new byte[notAackPacket.tokenLength];
				for (byte i=0; i< notAackPacket.tokenLength; i++){
					notAackPacket.token[i] = responsePacket.token[i];
				}
				notAackPacket.address = udp.remoteIP();
				notAackPacket.port = udp.remotePort();
				retransmitTime = (rand() % 1000 )+ 2000;
					
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
					byte plus =(block.blockNumber) * power(2,4+block.blockSize);	// index stringa well-known/core od ktorego znaki maja byc kopiowane
					memcpy(responsePacket.payload , wellKnownCore + plus, responsePacket.payloadLength);

					notAackPacket.block.blockSize = block.blockSize;
					notAackPacket.block.blockNumber = block.blockNumber;
					
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

					notAackPacket.block.blockSize = 1;
					notAackPacket.block.blockNumber = 0;
					
					delay(1);
				}
			}
		}
		else if (uriPathType == POTENTIOMETR)
		{
			if (isObserve) {
				registerObserver(cPacket);
			}

			uint16_t receiveValue=getPotentiometrValue();
			Serial.print("Potenc: "); Serial.println(receiveValue);

			if (receiveValue == 2000){
				responseErrorMessage(cPacket, NON, SERVICE_UNAVAILABLE, "Cannot connect to mini", 22);
				return;
			}
			
			responsePacket.optionsNumber =1;
			if (isObserve) {
				responsePacket.optionsNumber = 2;
			}

			Option* options = new Option[responsePacket.optionsNumber];
			deleteOptions = true;

			if (isObserve) {
				options[0].optionType = OBSERVE;
				options[0].optionLength = 1;
				options[0].optionValue = new byte[options[0].optionLength];
				options[0].optionValue[0] = seqNumber;

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
				responseErrorMessage(cPacket, NON, BAD_OPTION, "Option Observe not supported here", 33);
				return;
			}
			else {
				uint16_t receiveValue = getLampValue();
				
				if (receiveValue == 2000){
					responseErrorMessage(cPacket, NON, SERVICE_UNAVAILABLE, "Cannot connect to mini", 22);
					return;
				}

				responsePacket.optionsNumber =1;
				Option options[responsePacket.optionsNumber];

				responsePacket.optionsNumber =1;
				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 0;

				responsePacket.options = options;

				if (receiveValue >= 1000)	// obliczenie dlugosci payloadu
					responsePacket.payloadLength = 4;
				else if (receiveValue >= 100)
					responsePacket.payloadLength = 3;
				else if (receiveValue >= 10)
					responsePacket.payloadLength = 2;
				else
					responsePacket.payloadLength = 1;

				byte* payload = new byte[responsePacket.payloadLength];
				uint16_t prevValue = 0;
				for (byte i=0; i<responsePacket.payloadLength; i++ ) {	// wypelnienie payloadu
					payload[i] = receiveValue / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
					prevValue = (prevValue + payload[i]) * 10;
					payload[i] += '0';
				}

				responsePacket.payload = payload;
			}
		}
		else if (uriPathType == DELAY || uriPathType == DELAY_VARIATION)
		{
			if (isObservable) {
				responseErrorMessage(cPacket, RST, BAD_OPTION, "Option Observe not supported here", 33);
				return;
			}
			else {
				if(RTT != 0)
				{
          if (uriPathType == DELAY){
					  latency += RTT;
          }
          else{
            if(RTT > max_delay)
            {
              max_delay = RTT;
            }
            if(RTT < min_delay)
            {
              min_delay = RTT;
            }
//            Serial.print(lastRTTTime);
//            Serial.print(": ");
//            Serial.print(RTT);
//            Serial.print(" min_delay: ");
//            Serial.print(min_delay);
//            Serial.print(" max_delay: ");
//            Serial.println(max_delay);
            lastRTTTime = RTT;
          }
         
				}
//				if (isSize2Opt) {
//					responsePacket.optionsNumber = 3;
//				} else {
//					responsePacket.optionsNumber = 2;
//				}
        responsePacket.optionsNumber = 2;
				deleteOptions = true;
				Option* options = new Option[responsePacket.optionsNumber];

				options[0].optionType = CONTENT_FORMAT;
				options[0].optionLength = 0;

				options[1].optionType = BLOCK2;
				options[1].optionLength = 1;
				options[1].optionValue = new byte[options[1].optionLength];

//				if (isSize2Opt) {
//					options[2].optionType = SIZE2;
//					options[2].optionLength = 1;
//					options[2].optionValue = new byte[1];
//					options[2].optionValue[0] = 1;
//				}

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
          signed short result = 0;
          if (uriPathType == DELAY){
          result = latency/100;
					Serial.print("Average RTT (100 packets): ");
					Serial.println(latency/100);
					lossPacketNumber = 0;
					latency = 0;

          if (result >= 1000) // obliczenie wielkosci payloadu
            responsePacket.payloadLength = 4;
          else if (result >= 100)
            responsePacket.payloadLength = 3;
          else if (result >= 10)
            responsePacket.payloadLength = 2;
          else if( result < 10)
            responsePacket.payloadLength = 1;
          }
          else
          {
            result = max_delay - min_delay;
            Serial.print("Delay variation (100 packets): ");
            Serial.println(result);
            lossPacketNumber = 0;
            max_delay = 0;
            min_delay = 1000;

            if (result >= 1000) // obliczenie wielkosci payloadu
            responsePacket.payloadLength = 4;
            else if (result >= 100)
            responsePacket.payloadLength = 3;
            else if (result >= 10)
            responsePacket.payloadLength = 2;
            else if( result < 10)
            responsePacket.payloadLength = 1;
          }
          responsePacket.payload = new char[responsePacket.payloadLength];
      
          byte* payload = new byte[responsePacket.payloadLength];         
          uint16_t prevValue = 0; // zmienna pomocnicza do wypelniania payloadu
          for (byte i=0; i<responsePacket.payloadLength; i++ ) { // wypelnienie payloadu
            payload[i] = result / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
            prevValue = (prevValue + payload[i]) * 10;
            payload[i] += '0';
          }
          responsePacket.payload = payload;
          sendResponse(&responsePacket); 
          return;
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
    else if (uriPathType == LOSS)
    {
      if (isObservable) {
        responseErrorMessage(cPacket, RST, BAD_OPTION, "Option Observe not supported here", 33);
        return;
      }
      responsePacket.optionsNumber = 1;
      Option* options = new Option[responsePacket.optionsNumber];

      options[0].optionType = CONTENT_FORMAT;
      options[0].optionLength = 0;

      responsePacket.options = options;
      
      if (retransmitCounter >= 1000) // obliczenie wielkosci payloadu
            responsePacket.payloadLength = 4;
          else if (retransmitCounter >= 100)
            responsePacket.payloadLength = 3;
          else if (retransmitCounter >= 10)
            responsePacket.payloadLength = 2;
          else if( retransmitCounter < 10)
            responsePacket.payloadLength = 1;
            
          responsePacket.payload = new char[responsePacket.payloadLength];

          byte* payload = new byte[responsePacket.payloadLength];         
          uint16_t prevValue = 0; // zmienna pomocnicza do wypelniania payloadu
          for (byte i=0; i<responsePacket.payloadLength; i++ ) { // wypelnienie payloadu
            payload[i] = retransmitCounter / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
            prevValue = (prevValue + payload[i]) * 10;
            payload[i] += '0';
          }
          Serial.println(responsePacket.payloadLength);
          Serial.println(retransmitCounter);
          responsePacket.payload = payload;
          sendResponse(&responsePacket); 
          return;  
    }
		else {
			responsePacket.code = NOT_FOUND; //4.04
			responsePacket.payloadLength = 14;
			responsePacket.payload = new byte[responsePacket.payloadLength];
			memcpy(responsePacket.payload , "Wrong URI-PATH", responsePacket.payloadLength);
      Serial.println(uriPathType);
		}
	}
	else	// error
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
	if (responsePacket.type == CON){
		prevRetransmitTime= millis();
		isMessageConSent = true;
	}
	//delay(500);
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

		bool ok = registerObserverInMini();
		
		if (ok){
			observer.address = udp.remoteIP();
			observer.port = udp.remotePort();
			observer.tokenLength = cPacket->tokenLength;
			observer.token = new byte[observer.tokenLength];
			memcpy(observer.token, cPacket->token, observer.tokenLength);
			observersNumber++;
		}
		else{	// mini nie wyslalo potwierdzenia, brak lacznosci z mini
			responseErrorMessage(cPacket, NON, SERVICE_UNAVAILABLE, "Cannot connect to mini", 22);
		}
	}
}

void sendToObservers(uint16_t receiveValue) {	// wysylanie wiadomosci jesli jest wlaczone obserwowanie
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
		options[0].optionValue[0] = seqNumber++;
	
		options[1].optionType = CONTENT_FORMAT;
		options[1].optionLength = 0;
	
		responsePacket.options = options;
	
		if (receiveValue >= 1000)	// obliczenie wielkosci payloadu
			responsePacket.payloadLength = 4;
		else if (receiveValue >= 100)
			responsePacket.payloadLength = 3;
		else if (receiveValue >= 10)
			responsePacket.payloadLength = 2;
		else
			responsePacket.payloadLength = 1;
	
		byte* payload = new byte[responsePacket.payloadLength];
		uint16_t prevValue = 0;	// zmienna pomocnicza do wypelniania payloadu
		for (byte i=0; i<responsePacket.payloadLength; i++ ) { // wypelnienie payloadu
			payload[i] = receiveValue / pow(10,(responsePacket.payloadLength - i - 1)) - prevValue;
			prevValue = (prevValue + payload[i]) * 10;
			payload[i] += '0';
		}
	
		responsePacket.payload = payload;
//		Serial.print("payload: ");
//		for (byte i=0; i< responsePacket.payloadLength; i++) {
//			Serial.print(char(responsePacket.payload[i]));
//		}
//		Serial.println();
	
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
	else{	// jesli obserwowanie nie jest wlaczone, ale mini nadal wysyla (np. Uno zresetowane a mini ma wlaczone obserwowanie)
		unregisterObserverInMini();
	}
}

Block2Param parseBlock2(Option option) {
	Block2Param block;
	block.blockNumber = option.optionValue[0] >> 4;
	block.blockSize = byte(option.optionValue[0] << 5) >> 5;
	return block;
}

void stopObserving() {
	if (observersNumber > 0) {
		observersNumber = 0;
	}
	unregisterObserverInMini();
}

void retransmit(){

	Serial.println("retransmisja");
	Serial.print("block number: "); Serial.println(notAackPacket.block.blockNumber);
	
	CoapPacket responsePacket;
	responsePacket.optionsNumber = 0;
	responsePacket.payloadLength = 0;
	
	responsePacket.ver = 1;
	responsePacket.type = CON;
	responsePacket.tokenLength = notAackPacket.tokenLength;
	responsePacket.code = CONTENT;
	responsePacket.messageID[0] = notAackPacket.messageID[0];
	responsePacket.messageID[1] = notAackPacket.messageID[1] ;
	// TOKEN
	responsePacket.token = notAackPacket.token;

	responsePacket.optionsNumber = 2;
	Option* options = new Option[responsePacket.optionsNumber];

	options[0].optionType = CONTENT_FORMAT;
	options[0].optionLength = 1;
	options[0].optionValue = new byte[options[0].optionLength];
	options[0].optionValue[0] = LINK_FORMAT; //core link format

	options[1].optionType = BLOCK2;
	options[1].optionLength = 1;
	options[1].optionValue = new byte[options[1].optionLength];


	Block2Param block = notAackPacket.block;

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

	responsePacket.options = options;


	responsePacket.payloadLength = power(2,4+block.blockSize);

	if (!isNextBlock) {
		responsePacket.payloadLength = wellKnownSize - (block.blockNumber) * power(2,4+block.blockSize);
	}

	responsePacket.payload = new char[responsePacket.payloadLength];
	byte plus =(block.blockNumber) * power(2,4+block.blockSize);	// index stringa well-known/core od ktorego znaki maja byc kopiowane
	memcpy(responsePacket.payload , wellKnownCore + plus, responsePacket.payloadLength);
	
	Serial.print("payload length: "); Serial.println(responsePacket.payloadLength);
	sendResponse(&responsePacket, notAackPacket.address, notAackPacket.port);

	if (responsePacket.payloadLength > 0){
		delete [] responsePacket.payload;
	}
	for (byte i=0; i<responsePacket.optionsNumber; i++ ) {
		if (responsePacket.options[i].optionLength > 0) {
			delete [] responsePacket.options[i].optionValue;
		}
	}
	delete [] responsePacket.options;

	prevRetransmitTime = millis();
	retransmitCounter++;
	retransmitTime *= 2;
}

void responseForPing(CoapPacket *cPacket)
{
	CoapPacket responsePacket;

	responsePacket.ver = 1;
	responsePacket.type = RST;
	responsePacket.code = (byte)0;
	responsePacket.tokenLength = cPacket->tokenLength;
	responsePacket.messageID[0] = cPacket->messageID[0];
	responsePacket.messageID[1] = cPacket->messageID[1];

	responsePacket.optionsNumber = 0;
	responsePacket.payloadLength = 0;

	sendResponse(&responsePacket);
}

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

	byte message[packetSize];	// tablica bajtow, ktore beda wyslane 
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

	for (int i = 0; i < cPacket->tokenLength; i++){		//Token
		message[currentByteNumber++] = cPacket->token[i];
	}

	if(cPacket->optionsNumber > 0){		//Options
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




