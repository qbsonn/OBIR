enum messageTypes
{
	CON = 0, 
	NON = 1, 
	ACK = 2, 
	RST = 3
};

enum uriPaths
{
	WELL_KNOWN_CORE = 1, 
	POTENTIOMETR = 2, 
	LAMP = 3, 
	LOSS = 4,
  DELAY = 5,
  DELAY_VARIATION = 6
};

enum acceptedFormats
{
	PLAIN = 0, 
	LINK_FORMAT = 40
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

enum messageMiniTypes{
	GET_POTEN = 1,
	GET_LAMP = 2,
	SET_LAMP = 3,
	START_OBS = 4,
	NEW_VALUE_OBS = 5,
	STOP_OBS = 6,
	VALUE = 7,
	PACKET_LOST = 20,
	OK = 200
};




