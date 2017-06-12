#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

unsigned short sensorValue = 0;

RF24 radio(7,8);               
RF24Network network(radio);   

const uint16_t this_node = 01;    
const uint16_t other_node = 00;   

struct payload_t {                 
  unsigned short type;
  unsigned short value;
};

unsigned long interval = 1000;
unsigned long last_sent=0;             
unsigned long packets_sent;   

unsigned short lampValue = 0;
unsigned short prevPotentioValue = 0;

bool isObservable = false;

void setup() {
  Serial.begin(115200);
  Serial.println("MINI setuppp");

  pinMode(3, OUTPUT);
  SPI.begin();
  radio.begin();
  network.begin(30,this_node);  
  analogWrite(3,0);
}

void loop() {
  network.update();                 

  if (isObservable){
  	unsigned short newPotentioValue = analogRead(A0);
  	if (newPotentioValue != prevPotentioValue){
  		payload_t obsPayload = { 5, newPotentioValue };
  		prevPotentioValue = newPotentioValue;
  		sendPayloadToUno(obsPayload);
  	}
  }
             
  
    while ( network.available() ) {     
    
      RF24NetworkHeader header;        
      payload_t payload1;
      network.read(header,&payload1,sizeof(payload1));

      if (payload1.type == 1){ // GET Potencjometru
        sensorValue = analogRead(A0);
        Serial.print("Potenciometr: "); Serial.println(sensorValue);
      }
      else if (payload1.type == 2){ // GET Lampki
      	sensorValue = lampValue;
      	Serial.print("Lampa: "); Serial.println(sensorValue);
      }
      else if (payload1.type == 3){ // SET Lampki
      	lampValue = payload1.value;
      	analogWrite(3,payload1.value);
      	Serial.print("Lampa set: "); Serial.println(payload1.value);
		sensorValue = 200; // OK
      }
      else if (payload1.type == 4){	// Zacznij obserwowac
      	isObservable = true;
      }
      else if (payload1.type == 6){ // stop obserwowania
      	isObservable = false;
      }

        
	  payload_t payload = { sensorValue, sensorValue };
	  sendPayloadToUno(payload);
      
  }

  

}

void sendPayloadToUno(payload_t payload){
	RF24NetworkHeader header1( other_node);
	network.update(); 
	bool ok = network.write(header1,&payload,sizeof(payload));
}
    
    
  

