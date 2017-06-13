const uint16_t other_node = 01;
const uint16_t awaitTime = 1000; // liczba milisekund, oznacza czas oczekiwania na odpowiedz od Mini

RF24 radio(7, 8);               // nRF24L01(+) radio attached using Getting Started board
RF24Network network(radio);      // Network uses that radio
RF24NetworkHeader header(other_node);


// Metoda wysylajaca zadanie o zasob o okreslonym typie
bool sendGetValueMessage(byte type) {
	network.update();

	payload_t payload = { type, 0};
	bool ok = network.write(header, &payload, sizeof(payload));

	return ok;
}

// Metoda odbierajaca od mini wiadomosc o okreslonym typie. 
// Jesli pakiet nie zostanie odebrany w czasie awaitTime to zostanie uznany za zgubiony
payload_t receiveMessageFromMini(byte type) {
	unsigned long startTime = millis();
	while(millis() - startTime < awaitTime)
	{
		network.update();
		if (network.available()) {
			//RF24NetworkHeader header;
			payload_t payload;
			network.read(header, &payload, sizeof(payload));
			if (payload.type == type)
				return payload;
		}
	}
	payload_t payload = {PACKET_LOST, 0};
	return payload;
}


unsigned short getPotentiometrValue(){
	sendGetValueMessage(GET_POTEN);
	payload_t payload = receiveMessageFromMini(VALUE);
	if (payload.type != PACKET_LOST)
		return payload.value;
	else
		return 2000;
}

unsigned short getLampValue(){
	sendGetValueMessage(GET_POTEN);
	payload_t payload = receiveMessageFromMini(VALUE);
	if (payload.type != PACKET_LOST)
		return payload.value;
	else
		return 2000;
}

bool putLampValue(unsigned short value){
	network.update();

	payload_t payload = {SET_LAMP,value};
	bool ok = network.write(header, &payload, sizeof(payload));

	payload_t receivedPayload = receiveMessageFromMini(OK);
	if (receivedPayload.type != PACKET_LOST)
		return true;
	else
		return false;
}

bool registerObserverInMini(){
	network.update();
	
	payload_t payload = { START_OBS, 0};
	bool ok = network.write(header, &payload, sizeof(payload));

	payload_t receivedPayload = receiveMessageFromMini(OK);
	if (receivedPayload.type != PACKET_LOST)
		return true;
	else
		return false;
}

bool unregisterObserverInMini(){
	network.update();
	
	payload_t payload = { STOP_OBS, 0};
	bool ok = network.write(header, &payload, sizeof(payload));

	payload_t receivedPayload = receiveMessageFromMini(OK);
	if (receivedPayload.type != PACKET_LOST)
		return true;
	else
		return false;
}


