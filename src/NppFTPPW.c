/*
    NppFTP: FTP/SFTP functionality for Notepad++
    Copyright (C) 2010  Harry (harrybharry@users.sourceforge.net)
    GNU General Public License http://www.gnu.org/licenses/
*/
#include <openssl/des.h>

char * _DefaultKey = NULL;
gboolean _IsDefaultKey = TRUE;
const char * defaultString = "NppFTP00";	//must be 8 in length
const size_t KeySize = 8;

char* DES_encrypt(const char * key, int keysize, const char * data, int size, gboolean addZero, int type);
int FreeChar(char * string);
char* DataToHex(const char * data_, int len);
char* HexToData(const char * hex, int len, gboolean addZero);
char* Encrypt(const char * key, int keysize, const char * data, int size);
char* Decrypt(const char * key, int keysize, const char * data, gboolean addZero);

char* Encrypt(const char * key, int keysize, const char * data, int size) {
	if (size == -1)
		size = strlen(data);

	char * encdata = DES_encrypt(key, keysize, data, size, FALSE, DES_ENCRYPT);
	if (!encdata)
		return NULL;
	char * hexData = DataToHex(encdata, size);
	free(encdata);
	return hexData;
}

char* Decrypt(const char * key, int keysize, const char * data, gboolean addZero) {
	int size = strlen(data);
	char * encrdata = HexToData(data, size, FALSE);
	if (!encrdata)
		return NULL;

	size = size/2;
	char * decdata = DES_encrypt(key, keysize, encrdata, size, addZero, DES_DECRYPT);
	FreeChar(encrdata);

	return decdata;
}

char* DES_encrypt(const char * key, int keysize, const char * data, int size, gboolean addZero, int type) {
	char keybuf[KeySize];
	if (key == NULL) {
		memcpy(keybuf, _DefaultKey, KeySize);
		keysize = KeySize;
	} else {
		if (keysize == -1)
			keysize = strlen(key);
		if ((unsigned)keysize > KeySize)
			keysize = KeySize;
		memcpy(keybuf, key, keysize);
		size_t i;
		for(i = keysize; i < KeySize; i++)
			keybuf[i] = 0;
	}

	if (size == -1)
		size = strlen(data);

	char * decrypted = (char *)malloc((size+(addZero?1:0)+1)*sizeof(char));
	if (!decrypted)
		return NULL;

	if (addZero)
		decrypted[size] = 0;

	DES_cblock key2;
	DES_key_schedule schedule;
	memcpy(key2, keybuf, KeySize);
	DES_set_odd_parity(&key2);
	DES_set_key_checked(&key2, &schedule);

	int n = 0;
	DES_cfb64_encrypt((unsigned char*) data, (unsigned char *)decrypted, size, &schedule, &key2, &n, type);

	return decrypted;
}

char* DataToHex(const char * data_, int len) {
	static const char * table = "0123456789ABCDEF";

	const unsigned char * data = (unsigned char*)data_;

	if (len == -1)
		len = strlen(data_);

	char * hexString = (char *)malloc((len*2+1+1)*sizeof(char));
	int i;
	for(i = 0; i < len; i++) {
		unsigned int curVal = (unsigned int)data[i];
		hexString[i*2] = table[curVal/16];
		hexString[i*2+1] = table[curVal%16];
	}
	hexString[len*2] = 0;

	return hexString;
}

char* HexToData(const char * hex, int len, gboolean addZero) {
	if (len == -1)
		len = strlen(hex);

	if (len%2 != 0)
		return NULL;

	len = len/2;
	unsigned char * data = (unsigned char *)malloc((len+(addZero?1:0)+1)*sizeof(char));

	int i;
	for (i = 0; i < len; i++) {
		data[i] = 0;

		if (hex[i*2] <= '9')
			data[i] += (hex[i*2] - '0') * 16;
		else
			data[i] += ((hex[i*2] - 'A') + 10) * 16;

		if (hex[i*2+1] <= '9')
			data[i] += (hex[i*2+1] - '0');
		else
			data[i] += (hex[i*2+1] - 'A') + 10;
	}

	if (addZero) {
		data[len] = 0;
	}

	return (char*)data;
}

int FreeChar(char * string) {
	if (!string)
		return -1;

	free(string);
	return 0;
}
