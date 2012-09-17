//
//  BEFullNode.c
//  BitEagle-FullNode
//
//  Created by Matthew Mitchell on 14/09/2012.
//  Copyright (c) 2012 Matthew Mitchell
//
//  This file is part of BitEagle-FullNode.
//
//  BitEagle-FullNode is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  BitEagle-FullNode is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with BitEagle-FullNode.  If not, see <http://www.gnu.org/licenses/>.

//  SEE HEADER FILE FOR DOCUMENTATION

#include "BEFullNode.h"

//  Constructor

BEFullNode * BENewFullNode(void (*onErrorReceived)(CBError error,char *,...)){
	BEFullNode * self = malloc(sizeof(*self));
	if (NOT self) {
		onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Cannot allocate %i bytes of memory in BENewFullNode\n",sizeof(*self));
		return NULL;
	}
	CBGetObject(self)->free = BEFreeFullNode;
	if (BEInitFullNode(self,onErrorReceived))
		return self;
	free(self);
	return NULL;
}

//  Object Getter

BEFullNode * BEGetFullNode(void * self){
	return self;
}

//  Initialiser

bool BEInitFullNode(BEFullNode * self,void (*onErrorReceived)(CBError error,char *,...)){
	if (NOT CBInitNetworkCommunicator(CBGetNetworkCommunicator(self), onErrorReceived))
		return false;
	// Set network communicator fields.
	CBGetNetworkCommunicator(self)->blockHeight = 0;
	CBGetNetworkCommunicator(self)->callbackHandler = self;
	CBGetNetworkCommunicator(self)->flags = CB_NETWORK_COMMUNICATOR_AUTO_DISCOVERY | CB_NETWORK_COMMUNICATOR_AUTO_HANDSHAKE | CB_NETWORK_COMMUNICATOR_AUTO_PING;
	CBGetNetworkCommunicator(self)->version = CB_PONG_VERSION;
	CBNetworkCommunicatorSetAlternativeMessages(CBGetNetworkCommunicator(self), NULL, NULL);
	// Find home directory.
	const char * homeDir;
	struct passwd * pwd = getpwuid(getuid());
	if (NOT pwd)
		return false;
	homeDir = pwd->pw_dir;
	unsigned long homeLen = strlen(homeDir);
	// Open or create a new address store
	unsigned long dataDirLen = strlen(BE_DATA_DIRECTORY);
	char * addressFilePath = malloc(homeLen + dataDirLen + strlen(BE_ADDRESS_DATA_FILE) + 1);
	memcpy(addressFilePath, homeDir, homeLen);
	memcpy(addressFilePath + homeLen, BE_DATA_DIRECTORY, strlen(BE_DATA_DIRECTORY));
	strcpy(addressFilePath + homeLen + dataDirLen, BE_ADDRESS_DATA_FILE);
	self->addressFile = fopen(addressFilePath, "rb+");
	if (self->addressFile) {
		// The address store exists.
		free(addressFilePath);
		// Get the file length
		fseek(self->addressFile, 0, SEEK_END);
		unsigned long fileLen = ftell(self->addressFile);
		fseek(self->addressFile, 0, SEEK_SET);
		// Read file into a CBByteArray
		CBByteArray * buffer = CBNewByteArrayOfSize((uint32_t)fileLen, onErrorReceived);
		if (NOT buffer) {
			fclose(self->addressFile);
			return false;
		}
		if(fread(CBByteArrayGetData(buffer), fileLen, 1, self->addressFile) != fileLen){
			CBReleaseObject(buffer);
			fclose(self->addressFile);
			return false;
		}
		// Create the CBAddressManager
		CBGetNetworkCommunicator(self)->addresses = CBNewAddressManagerFromData(buffer, onErrorReceived, BEFullNodeOnBadTime);
		CBReleaseObject(buffer);
		if (NOT CBAddressManagerDeserialise(CBGetNetworkCommunicator(self)->addresses)){
			fclose(self->addressFile);
			CBReleaseObject(CBGetNetworkCommunicator(self)->addresses);
			onErrorReceived(CB_ERROR_INIT_FAIL,"There was an error when deserialising the CBAddressManager for the BEFullNode.");
			return false;
		}
	}else{
		// The address store does not exist
		CBGetNetworkCommunicator(self)->addresses = CBNewAddressManager(onErrorReceived, BEFullNodeOnBadTime);
		if (NOT CBGetNetworkCommunicator(self)->addresses)
			return false;
		// Create the file
		self->addressFile = fopen(addressFilePath, "wb");
		free(addressFilePath);
		if (NOT self->addressFile){
			CBReleaseObject(CBGetNetworkCommunicator(self)->addresses);
			return false;
		}
	}
	// Create block validator
	
	return true;
}

//  Destructor

void BEFreeFullNode(void * self){
	fclose(BEGetFullNode(self)->addressFile);
	CBReleaseObject(CBGetNetworkCommunicator(self)->addresses);
	CBFreeNetworkCommunicator(self);
}

//  Functions

void BEFullNodeOnBadTime(void * self){
	
}
