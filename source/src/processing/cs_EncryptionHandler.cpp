/*
 * Author: Crownstone Team
 * Copyright: Crownstone (https://crownstone.rocks)
 * Date: Jul 19, 2016
 * License: LGPLv3+, Apache License 2.0, and/or MIT (triple-licensed)
 */

#include <ble/cs_Stack.h>
#include <drivers/cs_Serial.h>
#include <events/cs_EventDispatcher.h>
#include <processing/cs_EncryptionHandler.h>
#include <storage/cs_State.h>

//#define TESTING_ENCRYPTION
#define LOGEncryption LOGnone

// Rotate functions for 16 bit integers
#define ROTL_16BIT(x, shift) ((x)<<(shift) | (x)>>(16-(shift)))
#define ROTR_16BIT(x, shift) ((x)>>(shift) | (x)<<(16-(shift)))

// Magic numbers for RC5 with 16 bit words.
#define RC5_16BIT_P 0xB7E1
#define RC5_16BIT_Q 0x9E37

void EncryptionHandler::init() {
	_defaultValidationKey.b = DEFAULT_SESSION_KEY;
	EventDispatcher::getInstance().addListener(this);
	TYPIFY(STATE_OPERATION_MODE) mode;
	State::getInstance().get(CS_TYPE::STATE_OPERATION_MODE, &mode, sizeof(mode));
	_operationMode = getOperationMode(mode);
}

uint16_t EncryptionHandler::calculateEncryptionBufferLength(uint16_t inputLength, EncryptionType encryptionType) {
	if (encryptionType == CTR || encryptionType == CTR_CAFEBABE) {
		// add the validation padding length to the raw data length
		uint16_t requiredLength = inputLength + VALIDATION_NONCE_LENGTH;

		uint16_t blockOverflow = requiredLength % SOC_ECB_CLEARTEXT_LENGTH;

		// check if we need an extra block
		requiredLength += blockOverflow > 0 ? SOC_ECB_CLEARTEXT_LENGTH - blockOverflow : 0;

		// add overhead
		requiredLength += PACKET_NONCE_LENGTH + USER_LEVEL_LENGTH;

		return requiredLength;
	}
	else {
		return SOC_ECB_CIPHERTEXT_LENGTH;
	}
}


uint16_t EncryptionHandler::calculateDecryptionBufferLength(uint16_t encryptedPacketLength) {
	uint16_t overhead = VALIDATION_NONCE_LENGTH + PACKET_NONCE_LENGTH + USER_LEVEL_LENGTH;
	// catch case where the length can be smaller than the overhead and the int overflows.
	if (encryptedPacketLength <= overhead) {
		return 0;
	}
	return encryptedPacketLength - overhead;
}

void EncryptionHandler::handleEvent(event_t & event) {
	switch (event.type) {
	case CS_TYPE::EVT_BLE_CONNECT:
		if (State::getInstance().isTrue(CS_TYPE::CONFIG_ENCRYPTION_ENABLED))
			_generateSessionNonce();
		break;
	default: {}
	}
}


uint8_t* EncryptionHandler::getSessionNonce() {
	return _sessionNonce;
}


void EncryptionHandler::closeConnectionAuthenticationFailure() {
	Stack::getInstance().disconnect();
}


bool EncryptionHandler::allowedToWrite() {
	return Stack::getInstance().isDisconnecting();
}


/**
 * This method does not use the AES CTR method but straight ECB.
 * It encrypts the block with the GUEST key.
 */
bool EncryptionHandler::_encryptECB(uint8_t* data, uint8_t dataLength, uint8_t* target, uint8_t targetLength,
		EncryptionAccessLevel userLevel, EncryptionType encryptionType) {
	LOGEncryption("Encrypt ECB");
	uint32_t err_code;
	uint8_t softdevice_enabled;
	err_code = sd_softdevice_is_enabled(&softdevice_enabled);
	if (!softdevice_enabled) {
		LOGw("Softdevice not enabled! Cannot call sd_ecb_block_encrypt");
		return false;
	}

	// total length must be exactly 1 block.
	if (targetLength != SOC_ECB_CIPHERTEXT_LENGTH) {
		LOGe("Invalid target buffer length %d %d",dataLength, targetLength);
		return false;
	}

	if ((encryptionType == ECB_GUEST_CAFEBABE && dataLength + DEFAULT_SESSION_KEY_LENGTH > SOC_ECB_CLEARTEXT_LENGTH) ||
			(encryptionType == ECB_GUEST && dataLength > SOC_ECB_CLEARTEXT_LENGTH)) {
		LOGe("Invalid input buffer length");
		return false;
	}

	// set the guest key as active encryption key
	if (_checkAndSetKey(userLevel) == false)
		return false;

	// set the cleartext to 0x0 so we automatically zeropad the cleartext
	memset(_block.cleartext, 0x0, SOC_ECB_CLEARTEXT_LENGTH);

	if (encryptionType == ECB_GUEST_CAFEBABE) {
		// copy the validation key into the cleartext
		memcpy(_block.cleartext, _defaultValidationKey.a, DEFAULT_SESSION_KEY_LENGTH);

		// after which, copy the data into the cleartext
		memcpy(_block.cleartext + DEFAULT_SESSION_KEY_LENGTH, data, dataLength);
	}
	else {
		// copy the data over into the cleartext
		memcpy(_block.cleartext, data, dataLength);
	}

	// encrypts the cleartext and puts it in ciphertext
	err_code = sd_ecb_block_encrypt(&_block);
	APP_ERROR_CHECK(err_code);

	// copy result into target.
	memcpy(target, _block.ciphertext, SOC_ECB_CIPHERTEXT_LENGTH);

	LOGEncryption("ECB encrypted");
	return true;
}



/**
 * Perform encryption:
 */
bool EncryptionHandler::encrypt(uint8_t* data, uint16_t dataLength, uint8_t* target, uint16_t targetLength,
		EncryptionAccessLevel userLevel, EncryptionType encryptionType) {
	if (encryptionType == CTR || encryptionType == CTR_CAFEBABE) {
		return _prepareEncryptCTR(data,dataLength,target,targetLength,userLevel,encryptionType);
	} else {
		return _encryptECB(data,dataLength,target,targetLength,userLevel,encryptionType);
	}
}

/**
 */
bool EncryptionHandler::_prepareEncryptCTR(uint8_t* data, uint16_t dataLength, uint8_t* target, uint16_t targetLength,
		EncryptionAccessLevel userLevel, EncryptionType encryptionType) {
	LOGEncryption("Encrypt CTR");

	// check if the userLevel has been set
	if (_checkAndSetKey(userLevel) == false) {
		LOGe("User level not set");
		return false;
	}

	uint16_t targetNetLength = targetLength - _overhead;

	// check if the input would still fit if the nonce is added.
	if (dataLength + VALIDATION_NONCE_LENGTH > targetNetLength) {
		LOGe(STR_ERR_BUFFER_NOT_LARGE_ENOUGH);
	//		LOGe("Length of input block would be larger than the output block if we add the validation nonce.");
		return false;
	}

	// generate a Nonce for this session and write it to the first 3 bytes of the target.
	_generateNonceInTarget(target);

//	 write the userLevel to the target
	target[PACKET_NONCE_LENGTH] = uint8_t(userLevel);

	// create the IV
	_createIV(_block.cleartext, target, encryptionType);

	uint8_t* validationNonce;
	if (encryptionType == CTR_CAFEBABE) {
		// in case we need a checksum but not a session, we use 0xcafebabe
		validationNonce = _defaultValidationKey.a;
	}
	else {
		// this will make sure the first 4 bytes of the payload are the validation nonce
		validationNonce = _sessionNonce;
	}

	if (_encryptCTR(validationNonce, data, dataLength, target+_overhead, targetNetLength) == false) {
		LOGe("Error while encrypting");
		return false;
	}

	return true;
}



bool EncryptionHandler::decrypt(uint8_t* encryptedDataPacket, uint16_t encryptedDataPacketLength, uint8_t* target,
		uint16_t targetLength, EncryptionAccessLevel& levelOfPackage, EncryptionType encryptionType) {
	if (!(encryptionType == CTR || encryptionType == CTR_CAFEBABE)) {
		LOGe("Cannot decrypt ECB");
		return false;
	}

	// check if the size of the encrypted data packet makes sense: min 1 block and overhead
	if (encryptedDataPacketLength < SOC_ECB_CIPHERTEXT_LENGTH + _overhead) {
		LOGe(STR_ERR_BUFFER_NOT_LARGE_ENOUGH);
//		LOGe("Encryted data packet is smaller than the minimum possible size of a block and overhead (20 bytes).");
		return false;
	}

	// check if the userLevel has is correct
	if (_checkAndSetKey(encryptedDataPacket[PACKET_NONCE_LENGTH]) == false)
		return false;

	// tell the reference the access level of the packet
	switch (encryptedDataPacket[PACKET_NONCE_LENGTH]) {
		case 0:
			levelOfPackage = ADMIN; break;
		case 1:
			levelOfPackage = MEMBER; break;
		case 2:
			levelOfPackage = BASIC; break;
		case 100:
			levelOfPackage = SETUP; break;
	}

	// the actual encrypted part is after the overhead
	uint16_t sourceNetLength = encryptedDataPacketLength - _overhead;

	// setup the IV
	_createIV(_block.cleartext, encryptedDataPacket, encryptionType);

	uint8_t* validationNonce;
	if (encryptionType == CTR_CAFEBABE) {
		validationNonce = _defaultValidationKey.a;
	}
	else {
		validationNonce = _sessionNonce;
	}

	if (_decryptCTR(validationNonce, encryptedDataPacket + _overhead, sourceNetLength, target, targetLength) == false) {
		LOGe("Error while decrypting");
		return false;
	}

	return true;
}

/**
 * This is where the magic happens. There are a few things that have to be done before this method is called:
 * 1) The correct key has to be set in the _block.key
 * 2) The correct Nonce has to be set in the _block.clearText
 *
 * The input length should be a multiple of 16 bytes.
 * It is possible that the target is smaller than the input, in this case the remainder is discarded.
 *
 */
bool EncryptionHandler::_decryptCTR(uint8_t* validationNonce, uint8_t* input, uint16_t inputLength, uint8_t* target, uint16_t targetLength) {
	LOGEncryption("Decrypt CTR");
	if (_validateBlockLength(inputLength) == false) {
		LOGe(STR_ERR_MULTIPLE_OF_16);
//		LOGe("Length of input block does not match the expected length (N*16 Bytes).");
		return false;
	}
	uint32_t err_code;

	// amount of blocks to loop over
	uint8_t blockOverflow = (targetLength + VALIDATION_NONCE_LENGTH) %  SOC_ECB_CIPHERTEXT_LENGTH;
	uint16_t blockCount = (targetLength + VALIDATION_NONCE_LENGTH) / SOC_ECB_CIPHERTEXT_LENGTH;
	blockCount += (blockOverflow > 0) ? 1 : 0;

	// variables to keep track of where data is written
	uint16_t inputReadIndex = 0;
	uint16_t writtenToTarget = 0;
	uint16_t targetLengthLeftToWrite = targetLength;
	uint16_t maxIterationWriteAmount;

	// encrypt all blocks using AES 128 CTR block mode.
	for (uint8_t counter = 0; counter < blockCount; counter++) {
		// prepare the IV for the next step by concatenating the nonce with the counter
		_block.cleartext[SOC_ECB_CLEARTEXT_LENGTH-1] = counter;

		// encrypts the cleartext and puts it in ciphertext
		err_code = sd_ecb_block_encrypt(&_block);
		APP_ERROR_CHECK(err_code);

		// XOR the ciphertext with the data to finish encrypting the block.
		for (uint8_t i = 0; i < SOC_ECB_CIPHERTEXT_LENGTH; i++) {
			inputReadIndex = i + counter * SOC_ECB_CIPHERTEXT_LENGTH;
			if (inputReadIndex < inputLength)
				_block.ciphertext[i] ^= input[inputReadIndex];
			else {
				LOGe("Length mismatch %d, counter: %d, i: %d", inputReadIndex, counter, i);
				return false;
			}
		}


		// check the validation nonce
		if (counter == 0) {
			if (_validateDecryption(_block.ciphertext, validationNonce) == false)
				return false;

			// the first iteration we can only write 12 relevant bytes to the target because the first 4 are the nonce.
			maxIterationWriteAmount = SOC_ECB_CIPHERTEXT_LENGTH - VALIDATION_NONCE_LENGTH;
			if (targetLengthLeftToWrite < maxIterationWriteAmount)
				maxIterationWriteAmount = targetLengthLeftToWrite;

			// we offset the pointer of the cipher text by the length of the validation length
			memcpy(target, _block.ciphertext + VALIDATION_NONCE_LENGTH, maxIterationWriteAmount);

		}
		else {
			// after the first iteration, we can write the full 16 bytes.
			maxIterationWriteAmount = SOC_ECB_CIPHERTEXT_LENGTH;
			if (targetLengthLeftToWrite < maxIterationWriteAmount)
				maxIterationWriteAmount = targetLengthLeftToWrite;

			memcpy(target + writtenToTarget, _block.ciphertext, maxIterationWriteAmount);
		}

		// update the variables that keep track of the writing.
		targetLengthLeftToWrite -= maxIterationWriteAmount;
		writtenToTarget += maxIterationWriteAmount;
	}

	if (targetLengthLeftToWrite != 0) {
		LOGe("Not all decrypted data has been written! %d", targetLengthLeftToWrite);
		return false;
	}

	return true;
}


/**
 * Checks if the first 4 bytes of the decrypted buffer match the session nonce or the cafebabe if useSessionNonce is false.
 */
bool EncryptionHandler::_validateDecryption(uint8_t* buffer, uint8_t* validationNonce) {
	for (uint8_t i = 0; i < VALIDATION_NONCE_LENGTH; i++) {
		if (buffer[i] != *(validationNonce + i)) {
			LOGe("Nonce mismatch");
			return false;
		}
	}
	return true;
}

bool EncryptionHandler::decryptBlockCTR(uint8_t* encryptedData, uint16_t encryptedDataLength, uint8_t* target, uint16_t targetLength, EncryptionAccessLevel accessLevel, uint8_t* nonce, uint8_t nonceLength) {
	if (encryptedDataLength < SOC_ECB_CIPHERTEXT_LENGTH || targetLength < SOC_ECB_CIPHERTEXT_LENGTH) {
		LOGe(STR_ERR_BUFFER_NOT_LARGE_ENOUGH);
		return false;
	}

	// Sets the key in _block.
	if (!_checkAndSetKey(accessLevel)) {
		return false;
	}
//	LOGd("key:");
//	BLEutil::printArray(_block.key, 16);

	// Set the IV in the cleartext. IV is simply nonce with zero padding, since the counter is 0.
	memset(_block.cleartext, 0x00, SOC_ECB_CLEARTEXT_LENGTH);
	memcpy(_block.cleartext, nonce, nonceLength);
//	LOGd("cleartext:");
//	BLEutil::printArray(_block.cleartext, 16);

	// encrypts the cleartext and puts it in ciphertext
	uint32_t errCode = sd_ecb_block_encrypt(&_block);
	APP_ERROR_CHECK(errCode);

	// XOR the ciphertext with the encrypted data to finish decrypting the block.
	for (uint8_t i = 0; i < SOC_ECB_CIPHERTEXT_LENGTH; i++) {
		target[i] = _block.ciphertext[i] ^ encryptedData[i];
	}

	return true;
}

bool EncryptionHandler::RC5InitKey(EncryptionAccessLevel accessLevel) {
	// Sets the key in _block.
	if (!_checkAndSetKey(accessLevel)) {
		return false;
	}
	return _RC5PrepareKey(_block.key, SOC_ECB_KEY_LENGTH);
}

// See https://en.wikipedia.org/wiki/RC5
bool EncryptionHandler::RC5Decrypt(uint16_t* encryptedData, uint16_t encryptedDataLength, uint16_t* target, uint16_t targetLength) {
	if (encryptedDataLength != 4 || targetLength != 4) {
		return false;
	}
	uint16_t a = encryptedData[0];
	uint16_t b = encryptedData[1];
	uint16_t sum;
	for (uint16_t i=RC5_ROUNDS; i>0; --i) {
		sum = b - _rc5SubKeys[2*i + 1];
		b = ROTR_16BIT(sum, a % 16) ^ a;
		sum = a - _rc5SubKeys[2*i];
		a = ROTR_16BIT(sum, b % 16) ^ b;
	}
	target[0] = a - _rc5SubKeys[0];
	target[1] = b - _rc5SubKeys[1];
	return true;
}

// See https://en.wikipedia.org/wiki/RC5
bool EncryptionHandler::_RC5PrepareKey(uint8_t* key, uint8_t keyLength) {
	if (keyLength != RC5_KEYLEN) {
		return false;
	}
	int keyLenWords = ((RC5_KEYLEN-1)/sizeof(uint16_t))+1; // c - The length of the key in words (or 1, if keyLength = 0).
	int loops = 3 * (RC5_NUM_SUBKEYS > keyLenWords ? RC5_NUM_SUBKEYS : keyLenWords);
	uint16_t L[keyLenWords] = {0}; // L[] - A temporary working array used during key scheduling. initialized to the key in words.

	for (int i = 0; i<keyLenWords; ++i) {
		L[i] = (key[2*i+1] << 8) + key[2*i];
//		LOGd("L[i]=%u", L[i]);
	}

	_rc5SubKeys[0] = RC5_16BIT_P;
	for (int i = 1; i < RC5_NUM_SUBKEYS; ++i) {
		_rc5SubKeys[i] = _rc5SubKeys[i-1] + RC5_16BIT_Q;
	}

	uint16_t i = 0;
	uint16_t j = 0;
	uint16_t a = 0;
	uint16_t b = 0;
	uint16_t sum;
	for (int k=0; k<loops; ++k) {
//		LOGd("i=%u j=%u a=%u b=%u L[j]=%u key[i]=%u", i, j, a, b, L[j], _rc5SubKeys[i]);
		sum = _rc5SubKeys[i] + a + b;
		a = ROTL_16BIT(sum, 3);
		_rc5SubKeys[i] = a;
		sum = L[j] + a + b;
		b = ROTL_16BIT(sum, (a+b) % 16);
		L[j] = b;
//		LOGd("  i=%u j=%u a=%u b=%u L[j]=%u key[i]=%u", i, j, a, b, L[j], _rc5SubKeys[i]);
		++i;
		++j;
		i %= RC5_NUM_SUBKEYS;
		j %= keyLenWords;
	}
//	LOGi("key=");
//	BLEutil::printArray(key, keyLength);
//	for (int i=0; i<RC5_NUM_SUBKEYS; ++i) {
//		LOGi("RC5 key[%i] = %u", i, _rc5SubKeys[i]);
//	}
	return true;
}


/**
 * This is where the magic happens. There are a few things that have to be done before this method is called:
 * 1) The correct key has to be set in the _block.key
 * 2) The correct Nonce has to be set in the _block.clearText
 *
 * The outputLength should be larger than 0 and a multiple of 16 or this will return false.
 * It is possible that the input is smaller than the output, in this case it will be zero padded.
 *
 * The validation nonce is automatically added to the input.
 */
bool EncryptionHandler::_encryptCTR(uint8_t* validationNonce, uint8_t* input, uint16_t inputLength, uint8_t* output, uint16_t outputLength) {
	if (_validateBlockLength(outputLength) == false) {
		LOGe(STR_ERR_MULTIPLE_OF_16);
//		LOGe("Length of output block does not match the expected length (N*16 Bytes).");
		return false;
	}

	uint32_t err_code;

	// amount of blocks to loop over
	uint16_t blockCount = outputLength / SOC_ECB_CIPHERTEXT_LENGTH;

	// shift is the position in the data that is being encrypted.
	uint16_t shift = 0;

	uint32_t inputReadIndex = 0;

#ifdef TESTING_ENCRYPTION
	for (uint8_t i = 0; i < SOC_ECB_KEY_LENGTH; i++) {
		LOGi("key: %d", _block.key[i]);
	}
#endif

	// encrypt all blocks using AES 128 CTR block mode.
	for (uint8_t counter = 0; counter < blockCount; counter++) {
		// prepare the nonce for the next step by concatenating the nonce with the counter
		_block.cleartext[SOC_ECB_CLEARTEXT_LENGTH-1] = counter;

#ifdef TESTING_ENCRYPTION
	for (uint8_t i = 0; i < SOC_ECB_CLEARTEXT_LENGTH; i++) {
		LOGi("IV: %d", _block.cleartext[i]);
	}
#endif
		// encrypts the cleartext and puts it in ciphertext
		err_code = sd_ecb_block_encrypt(&_block);
		APP_ERROR_CHECK(err_code);

		// calculate the location in the byte array that we are using
		shift = counter * SOC_ECB_CIPHERTEXT_LENGTH;

		// XOR the ciphertext with the data to finish encrypting the block.
		for (uint8_t i = 0; i < SOC_ECB_CIPHERTEXT_LENGTH; i++) {
			// if we are at the first block, we will add the validation nonce (VN) to the first 4 bytes
			if (shift == 0 && i < VALIDATION_NONCE_LENGTH) {
				_block.ciphertext[i] ^= *(validationNonce + i);
			} else {
				inputReadIndex = i + shift - VALIDATION_NONCE_LENGTH;

				if (inputReadIndex < inputLength)
					_block.ciphertext[i] ^= input[inputReadIndex];
				else
					_block.ciphertext[i] ^= 0; // zero padding the data to fit in the block.
			}
		}


		// copy the encrypted block over to the target
		// the buffer is shifted by the block count (output + shift -. pointer to output[shift])
		memcpy(output + shift, _block.ciphertext, SOC_ECB_CIPHERTEXT_LENGTH);
	}

	return true;
}


/**
 * Check if the key that we need is set in the memory and if so, set it into the encryption block
 */
bool EncryptionHandler::_checkAndSetKey(uint8_t userLevel) {
	// check if the userLevel is set.
	if (userLevel == NOT_SET) {
		LOGe("User level is not set.");
		return false;
	}

	CS_TYPE keyConfigType;
	switch (userLevel) {
	case ADMIN:
		keyConfigType = CS_TYPE::CONFIG_KEY_ADMIN;
		break;
	case MEMBER:
		keyConfigType = CS_TYPE::CONFIG_KEY_MEMBER;
		break;
	case BASIC:
		keyConfigType = CS_TYPE::CONFIG_KEY_BASIC;
		break;
	case SERVICE_DATA:
		keyConfigType = CS_TYPE::CONFIG_KEY_SERVICE_DATA;
		break;
	case LOCALIZATION:
		keyConfigType = CS_TYPE::CONFIG_KEY_LOCALIZATION;
		break;
	case SETUP: {
		if (_operationMode == OperationMode::OPERATION_MODE_SETUP && _setupKeyValid) {
			memcpy(_block.key, _setupKey, SOC_ECB_KEY_LENGTH);
			return true;
		}
		// This error is generated once on boot
		LOGe("Can't use this setup key (yet)");
		return false;
	}
	default:
		LOGe("Invalid user level");
		return false;
	}

	// get the key from the storage
	static int log_once = 1;
	if (log_once) {
		LOGd("Get key - level %i - from storage", userLevel);
		log_once = 0;
	}
	State::getInstance().get(keyConfigType, _block.key, SOC_ECB_CIPHERTEXT_LENGTH);
	return true;
}

/**
 * This method will fill the buffer with 3 random bytes. These are included in the message
 */
void EncryptionHandler::_generateNonceInTarget(uint8_t* target) {
#ifdef TESTING_ENCRYPTION
	memset(target, 128 ,PACKET_NONCE_LENGTH);
#else
	RNG::fillBuffer(target, PACKET_NONCE_LENGTH);
#endif
}

/**
 * This method will fill the buffer with 5 random bytes. This is done on connect and is only valid once.
 */
void EncryptionHandler::_generateSessionNonce() {
#ifdef TESTING_ENCRYPTION
	memset(_sessionNonce, 64 ,SESSION_NONCE_LENGTH);
#else
	RNG::fillBuffer(_sessionNonce, SESSION_NONCE_LENGTH);
#endif
	event_t event(CS_TYPE::EVT_SESSION_NONCE_SET, _sessionNonce, SESSION_NONCE_LENGTH);
	EventDispatcher::getInstance().dispatch(event);
}

/**
 * The Nonce of the CTR method is made up of 8 random bytes:
 * 	- 3 from the packet nonce
 * 	- 5 from the session nonce
 * We use the term IV (initialization vector) for this because we have enough Nonce terms floating around.
 */
void EncryptionHandler::_createIV(uint8_t* IV, uint8_t* nonce, EncryptionType encryptionType) {
	// Set the nonce to zero to ensure it is consistent.
	// Since we only have 1 uint8_t as counter doing this once is enough.
	memset(IV, 0x00, SOC_ECB_CLEARTEXT_LENGTH);
	// half the text is nonce, the other half is a counter
	memcpy(IV, nonce, PACKET_NONCE_LENGTH);

	if (encryptionType == CTR_CAFEBABE) {
		// in case we do not use the session nonce, we use the cafebabe as a validation
		memcpy(IV + PACKET_NONCE_LENGTH, _defaultValidationKey.a, SESSION_NONCE_LENGTH);
	}
	else {
		// copy the session nonce over into the target
		memcpy(IV + PACKET_NONCE_LENGTH, _sessionNonce, SESSION_NONCE_LENGTH);
	}
}


/**
 * Verify if the block length is correct
 */
bool EncryptionHandler::_validateBlockLength(uint16_t length) {
	if (length % SOC_ECB_CLEARTEXT_LENGTH != 0 || length == 0) {
		return false;
	}
	return true;
}



bool EncryptionHandler::allowAccess(EncryptionAccessLevel minimum, EncryptionAccessLevel provided) {
	// always allow access when encryption is disabled.
	if (State::getInstance().isTrue(CS_TYPE::CONFIG_ENCRYPTION_ENABLED) == false) {
		return true;
	}

	if (minimum != ENCRYPTION_DISABLED) {
		if (_operationMode == OperationMode::OPERATION_MODE_SETUP && provided == SETUP && _setupKeyValid) {
			return true;
		}

		if (minimum == SETUP && (provided != SETUP || _operationMode != OperationMode::OPERATION_MODE_SETUP || !_setupKeyValid)) {
			LOGw("Setup mode required");
			return false;
		}

		// 0 is the highest possible value: ADMIN. If the provided is larger than the minimum, it is not allowed.
		if (provided > minimum) {
			LOGw("Insufficient access");
			return false;
		}
	}

	return true;
}

uint8_t* EncryptionHandler::generateNewSetupKey() {
	if (_operationMode == OperationMode::OPERATION_MODE_SETUP) {
		RNG::fillBuffer(_setupKey, SOC_ECB_KEY_LENGTH);
		_setupKeyValid = true;
	}
	else {
		_setupKeyValid = false;
	}

	return &(_setupKey[0]);
}

void EncryptionHandler::invalidateSetupKey() {
	_setupKeyValid = false;
}
