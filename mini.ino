#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

unsigned short sensorValue = 0;

RF24 radio(7,8);               
RF24Network network(radio);   

const uint16_t this_node = 01;    
const uint16_t other_node = 00;   

struct payload_t {                 
  unsigned long ms;
  unsigned short value;
};

unsigned long interval = 1000;
unsigned long last_sent=0;             
unsigned long packets_sent;          

void setup() {
  Serial.begin(115200);
  Serial.println("MINI setup");
 
  SPI.begin();
  radio.begin();
  network.begin(30,this_node);  
}

void loop() {
  network.update();                 

  
             
  
    while ( network.available() ) {     
    
      RF24NetworkHeader header;        
      payload_t payload1;
      network.read(header,&payload1,sizeof(payload1));  
           
      sensorValue = analogRead(A0);
      Serial.println(sensorValue);
       
      payload_t payload = { millis(), sensorValue };
      RF24NetworkHeader header1( other_node);
      bool ok = network.write(header1,&payload,sizeof(payload));
    
  }

}
    
    
  

