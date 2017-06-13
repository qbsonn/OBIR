uint16_t power(byte base, byte number) {
	uint16_t value = 1;
	for (byte i=0; i< number; i++) {
		value = value * base;
	}
	return value;
}

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

