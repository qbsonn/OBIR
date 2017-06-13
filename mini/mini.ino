#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

struct payload_t {
	unsigned short type;
	unsigned short value;
};

enum messageTypes{
	GET_POTEN = 1,
	GET_LAMP = 2,
	SET_LAMP = 3,
	START_OBS = 4,
	NEW_VALUE_OBS = 5,
	STOP_OBS = 6,
	VALUE = 7,
	OK = 200
};

const uint16_t this_node = 01;	// identyfikator tego wezla (Mini)
const uint16_t other_node = 00;	// identyfikator Uno
const int sendInterval = 1000; // co ile czasu jest mierzona wartosc potencjometru i wysylana do Uno, jesli wlaczone obserwowanie

RF24 radio(7,8);	// 7 i 8 to numery wyprowadzeń plytki Arduino, do ktorych dolaczono odpowiednio sygnaly CE i CSN ukladu radiowego
RF24Network network(radio);
RF24NetworkHeader header1(other_node);

unsigned short lampValue = 0;	// aktualna wartosc na lampie
signed short prevPotentioValue = 0;	// ostatnia wartosc zmierzona na potencjometrze (zmienna uzywana do zasobu obserwowalnego)
unsigned long lastSentMilis = 0;	// czas ostatniego wyslania wiadomosci observe

bool isObservable = false;	// oznacza czy jest wlaczona obserwacja zasobu

void setup() {
	Serial.begin(115200);
	pinMode(3, OUTPUT);	// latarka
	SPI.begin();
	radio.begin();
	network.begin(30,this_node);	// ustalenie kanalu komunikacji radiowej i identyfikatora swojego wezla

	analogWrite(3,0);
	Serial.println("MINI setup finished");
}

void loop() {
	network.update();

	if (isObservable && (millis() - lastSentMilis) >  sendInterval) {	// jesli jest wlaczana obserwacja zasobu i uplynal odpowiedni czas od ostatniego wyslania wiadomosci
		signed short newPotentioValue = analogRead(A0);
		if (newPotentioValue > prevPotentioValue + 20 || newPotentioValue < prevPotentioValue - 20) { // porownanie czy wartosc zmienila sie wystarczajaco bardzo
		
			payload_t obsPayload = { NEW_VALUE_OBS, newPotentioValue };
			prevPotentioValue = newPotentioValue;

			sendPayloadToUno(obsPayload);
			lastSentMilis = millis();
		}
	}


	while ( network.available() ) {

		RF24NetworkHeader header;
		payload_t receivedPayload;
		network.read(header,&receivedPayload,sizeof(receivedPayload));
		unsigned short sensorValue = 0;
		unsigned short messageType = 0;
		Serial.print("Received: "); Serial.println(receivedPayload.type);

		if (receivedPayload.type == GET_POTEN) { // GET Potencjometru
			sensorValue = analogRead(A0);
			messageType = VALUE;
		}
		else if (receivedPayload.type == GET_LAMP) { // GET Lampki
			sensorValue = lampValue;
			messageType = VALUE;
		}
		else if (receivedPayload.type == SET_LAMP) { // SET Lampki
			lampValue = receivedPayload.value;
			analogWrite(3,receivedPayload.value);
			Serial.println(lampValue);
			messageType = OK;
		}
		else if (receivedPayload.type == START_OBS) {	// Zacznij obserwowac
			isObservable = true;
			prevPotentioValue = analogRead(A0);
			messageType = OK;
		}
		else if (receivedPayload.type == STOP_OBS) { // stop obserwowania
			isObservable = false;
			messageType = OK;
		}

		payload_t payload = { messageType, sensorValue };
		sendPayloadToUno(payload);
	}
}

void sendPayloadToUno(payload_t payload) {
	network.update();
	bool ok = network.write(header1,&payload,sizeof(payload));
}




