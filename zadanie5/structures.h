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
