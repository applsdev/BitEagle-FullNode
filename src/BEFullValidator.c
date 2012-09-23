//
//  BEFullValidator.c
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

#include "BEFullValidator.h"

//  Constructor

BEFullValidator * BENewFullValidator(char * homeDir, void (*onErrorReceived)(CBError error,char *,...)){
	BEFullValidator * self = malloc(sizeof(*self));
	if (NOT self) {
		onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Cannot allocate %i bytes of memory in BENewFullNode\n",sizeof(*self));
		return NULL;
	}
	CBGetObject(self)->free = BEFreeFullValidator;
	if (BEInitFullValidator(self, homeDir, onErrorReceived))
		return self;
	free(self);
	return NULL;
}

//  Object Getter

BEFullValidator * BEGetFullValidator(void * self){
	return self;
}

//  Initialiser

bool BEInitFullValidator(BEFullValidator * self, char * dataDir, void (*onErrorReceived)(CBError error,char *,...)){
	if (NOT CBInitObject(CBGetObject(self)))
		return false;
	self->onErrorReceived = onErrorReceived;
	// Get the maximum number of allowed files
	struct rlimit fileLim;
	if(getrlimit(RLIMIT_NOFILE,&fileLim)){
		onErrorReceived(CB_ERROR_INIT_FAIL,"Could not get RLIMIT_NOFILE limits.");
		return false;
	}
	// Ensure highest limit
	fileLim.rlim_cur = BE_MIN(OPEN_MAX,fileLim.rlim_max); // OPEN_MAX needed for OSX. Surely this is against the POSIX standards? Apple's fault I guess.
	if(setrlimit(RLIMIT_NOFILE,&fileLim)){
		onErrorReceived(CB_ERROR_INIT_FAIL,"Could not set RLIMIT_NOFILE limit.");
		return false;
	}
	self->dataDir = malloc(strlen(dataDir) + 1);
	if (NOT self->dataDir) {
		onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for the data directory in BEInitFullValidator.",strlen(dataDir) + 1);
		return false;
	}
	strcpy(self->dataDir, dataDir);
	self->fileDescLimit = fileLim.rlim_cur - 3; // Minus validator file, address file and previous output file.
	// Get validation data or create a new data store
	unsigned long dataDirLen = strlen(dataDir);
	char * validatorFilePath = malloc(dataDirLen + strlen(BE_ADDRESS_DATA_FILE) + 1);
	memcpy(validatorFilePath, dataDir, dataDirLen);
	strcpy(validatorFilePath + dataDirLen, BE_VALIDATION_DATA_FILE);
	self->validatorFile = fopen(validatorFilePath, "rb+");
	if (self->validatorFile) {
		// Validation data exists
		self->numOpenBlockFiles = 0;
		free(validatorFilePath);
		// Get the file length
		fseek(self->validatorFile, 0, SEEK_END);
		unsigned long fileLen = ftell(self->validatorFile);
		fseek(self->validatorFile, 0, SEEK_SET);
		// Copy file contents into buffer.
		CBByteArray * buffer = CBNewByteArrayOfSize((uint32_t)fileLen, onErrorReceived);
		if (NOT buffer) {
			fclose(self->validatorFile);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create buffer of size %u.",fileLen);
			return false;
		}
		size_t res = fread(CBByteArrayGetData(buffer), 1, fileLen, self->validatorFile);
		if(res != fileLen){
			CBReleaseObject(buffer);
			fclose(self->validatorFile);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not read %u bytes of data into buffer. fread returned %u",fileLen,res);
			return false;
		}
		// Deserailise data
		if (buffer->length >= 56){
			uint8_t cursor = 0;
			self->mainBranch = CBByteArrayGetByte(buffer, cursor);
			cursor++;
			self->numBranches = CBByteArrayGetByte(buffer, cursor);
			cursor++;
			if (self->numBranches && self->numBranches <= BE_MAX_BRANCH_CACHE) {
				bool err = false;
				uint8_t x = 0;
				for (; x < self->numBranches; x++) {
					if (buffer->length < cursor + 54){
						err = true;
						onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the number of references %u < %u",buffer->length, cursor + 54);
						break;
					}
					self->branches[x].numRefs = CBByteArrayReadInt32(buffer, cursor);
					if (buffer->length < cursor + 50 + self->branches[x].numRefs*60) {
						err = true;
						onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the references %u < %u",buffer->length, cursor + 50 + self->branches[x].numRefs*60);
						break;
					}
					self->branches[x].references = malloc(sizeof(*self->branches[x].references) * self->branches[x].numRefs);
					if (NOT self->branches[x].references) {
						err = true;
						onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for references in BEInitFullValidator.",sizeof(*self->branches[x].references) * self->branches[x].numRefs);
						break;
					}
					cursor += 4;
					for (uint32_t y = 0; y < self->branches[x].numRefs; y++) {
						memcpy(self->branches[x].references[y].blockHash, CBByteArrayGetData(buffer) + cursor, 32);
						cursor += 32;
						self->branches[x].references[y].ref.fileID = CBByteArrayReadInt16(buffer, cursor);
						cursor += 2;
						self->branches[x].references[y].ref.filePos = CBByteArrayReadInt64(buffer, cursor);
						cursor += 8;
						self->branches[x].references[y].height = CBByteArrayReadInt32(buffer, cursor);
						cursor += 4;
						self->branches[x].references[y].target = CBByteArrayReadInt32(buffer, cursor);
						cursor += 4;
						self->branches[x].references[y].time = CBByteArrayReadInt32(buffer, cursor);
						cursor += 4;
						self->branches[x].references[y].prevBranch = CBByteArrayGetByte(buffer, cursor);
						cursor += 1;
						self->branches[x].references[y].prevIndex = CBByteArrayReadInt32(buffer, cursor);
						cursor += 4;
						self->branches[x].references[y].branch = CBByteArrayGetByte(buffer, cursor);
						cursor += 1;
					}
					if (buffer->length < cursor + 50) {
						err = true;
						onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the remaining branch %u < %u",buffer->length, cursor + 50);
						break;
					}
					self->branches[x].lastBlock = CBByteArrayReadInt32(buffer, cursor);
					cursor += 4;
					self->branches[x].lastRetargetTime = CBByteArrayReadInt32(buffer, cursor);
					cursor += 4;
					self->branches[x].lastTarget = CBByteArrayReadInt32(buffer, cursor);
					cursor += 4;
					for (uint8_t x = 0; x < 6; x++) {
						self->branches[x].prevTime[x] = CBByteArrayReadInt32(buffer, cursor);
						cursor += 8;
					}
					self->branches[x].parentBranch = CBByteArrayGetByte(buffer, cursor);
					cursor++;
					self->branches[x].startHeight = CBByteArrayReadInt32(buffer, cursor);
					cursor+= 4;
					self->branches[x].startValidation = CBByteArrayReadInt32(buffer, cursor);
					cursor+= 4;
					self->branches[x].numUnspentOutputs = CBByteArrayReadInt32(buffer, cursor);
					cursor += 4;
					if (buffer->length >= cursor + self->branches[x].numUnspentOutputs*52 + 1) {
						if (self->branches[x].numUnspentOutputs) {
							self->branches[x].unspentOutputs = malloc(sizeof(*self->branches[x].unspentOutputs) * self->branches[x].numUnspentOutputs);
							if (NOT self->branches[x].unspentOutputs)
								onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for unspent outputs in BEInitFullValidator for branch %u.",sizeof(*self->branches[x].unspentOutputs) * self->branches[x].numUnspentOutputs,x);
							for (uint32_t x = 0; x < self->branches[x].numUnspentOutputs; x++) {
								memcpy(self->branches[x].unspentOutputs[x].outputHash, CBByteArrayGetData(buffer) + cursor, 32);
								cursor += 32;
								self->branches[x].unspentOutputs[x].outputIndex = CBByteArrayReadInt32(buffer, cursor);
								cursor += 4;
								self->branches[x].unspentOutputs[x].ref.fileID = CBByteArrayReadInt16(buffer, cursor);
								cursor += 2;
								self->branches[x].unspentOutputs[x].ref.filePos = CBByteArrayReadInt64(buffer, cursor);
								cursor += 8;
								self->branches[x].unspentOutputs[x].height = CBByteArrayReadInt32(buffer, cursor);
								cursor += 4;
								self->branches[x].unspentOutputs[x].coinbase = CBByteArrayGetByte(buffer, cursor);
								cursor += 1;
								self->branches[x].unspentOutputs[x].branch = CBByteArrayGetByte(buffer, cursor);
								cursor += 1;
							}
						}
						// Now do orhpans
						self->orphansStart = cursor;
						self->numOrphans = CBByteArrayGetByte(buffer, cursor);
						cursor++;
						if (buffer->length >= cursor + self->numOrphans*82){
							for (uint8_t x = 0; x < self->numOrphans; x++) {
								bool err = false;
								CBByteArray * data = CBNewByteArraySubReference(buffer, cursor, buffer->length - 56);
								if (data) {
									self->orphans[x] = CBNewBlockFromData(data, onErrorReceived);
									if (self->orphans[x]) {
										// Deserialise
										uint32_t len = CBBlockDeserialise(self->orphans[x], true);
										if (len) {
											data->length = len;
											// Make orhpan block data a copy of the subreference.
											CBGetMessage(self->orphans[x])->bytes = CBByteArrayCopy(data);
											if (CBGetMessage(self->orphans[x])->bytes) {
												CBReleaseObject(data);
												CBReleaseObject(data);
											}else{
												onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create byte copy for orphan %u",x);
												err = true;
											}
										}else{
											onErrorReceived(CB_ERROR_MESSAGE_SERIALISATION_BAD_BYTES,"Could not deserailise orphan %u",x);
											err = true;
										}
										if (err)
											CBReleaseObject(self->orphans[x]);
									}else{
										onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create block object for orphan %u",x);
										err = true;
									}
									if (err)
										CBReleaseObject(data);
								}else{
									onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create byte reference for orphan %u",x);
									err = true;
								}
								if (err){
									for (uint8_t y = 0; y < x; y++)
										CBReleaseObject(self->orphans[y]);
									CBReleaseObject(buffer);
									fclose(self->validatorFile);
									free(self->dataDir);
									return false;
								}
							}
							// Success
							CBReleaseObject(buffer);
							// All done
							return true;
						}else
							onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"There is not enough data for the orhpans. %i < %i",buffer->length, cursor + self->numOrphans*82);
					}else
						onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"There is not enough data for the unspent outputs. %i < %i",buffer->length, cursor + self->branches[x].numUnspentOutputs*51);
					// Get work
					self->branches[x].work.length = CBByteArrayGetByte(buffer, cursor);
					cursor++;
					if (buffer->length < cursor + self->branches[x].work.length + 4) {
						err = true;
						onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the branch work %u < %u",buffer->length, cursor + self->branches[x].work.length + 4);
						break;
					}
					memcpy(self->branches[x].work.data,CBByteArrayGetData(buffer) + cursor,self->branches[x].work.length);
					cursor += self->branches[x].work.length;
				}
				if (err) {
					// Free branch data
					for (uint8_t y = 0; y < x; y++)
						free(self->branches[y].references);
				}
			}else
				onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"The number of branches is invalid.");
		}else
			onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for anything %u < 56",buffer->length);
		CBReleaseObject(buffer);
		fclose(self->validatorFile);
		free(self->dataDir);
		return false;
	}else{
		// Create initial data
		// Create directory if it doesn't already exist
		if(mkdir(dataDir, S_IREAD | S_IWRITE))
			if (errno != EEXIST){
				free(validatorFilePath);
				free(self->dataDir);
				onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create the data directory.");
				return false;
			}
		// Open validator file
		self->validatorFile = fopen(validatorFilePath, "wb");
		if (NOT self->validatorFile){
			free(validatorFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not open the validator file.");
			return false;
		}
		self->numOrphans = 0;
		// Make branch with genesis block
		self->numBranches = 1;
		self->branches[0].lastBlock = 0;
		self->branches[0].lastRetargetTime = 1231006505;
		self->branches[0].lastTarget = CB_MAX_TARGET;
		for (uint8_t x = 0; x < 6; x++)
			self->branches[0].prevTime[x] = 1231006505; // Genesis block time
		self->branches[0].startHeight = 0;
		self->branches[0].numRefs = 1;
		self->branches[0].references = malloc(sizeof(*self->branches[0].references));
		self->branches[0].startValidation = 1;
		self->branches[0].numUnspentOutputs = 0;
		self->branches[0].unspentOutputs = NULL;
		if (NOT self->branches[0].references) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the genesis reference in BEInitFullValidator.",sizeof(*self->branches[0].references));
			return false;
		}
		uint8_t genesisHash[32] = {0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00};
		memcpy(self->branches[0].references[0].blockHash,genesisHash,32);
		self->branches[0].references[0].ref.fileID = 0;
		self->branches[0].references[0].ref.filePos = 0;
		self->branches[0].references[0].height = 0;
		self->branches[0].work.length = 1;
		self->branches[0].work.data = malloc(1);
		if (NOT self->branches[0].work.data) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate 1 byte of memory for the initial branch work in BEInitFullValidator.");
			return false;
		}
		self->branches[0].work.data[0] = 0;
		self->mainBranch = 0;
		// Write genesis block to the first block file
		char * blockFilePath = malloc(dataDirLen + 12);
		if (NOT blockFilePath) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the first block file path in BEInitFullValidator.",dataDirLen + 12);
			return false;
		}
		memcpy(blockFilePath, dataDir, dataDirLen);
		strcpy(blockFilePath + dataDirLen, "blocks0.dat");
		self->branches[0].blockFiles = malloc(sizeof(*self->branches[0].blockFiles));
		if (NOT self->branches[0].blockFiles) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the blockFiles list in BEInitFullValidator",sizeof(*self->branches[0].blockFiles));
			return false;
		}
		self->branches[0].blockFiles[0].fileID = 0;
		self->branches[0].blockFiles[0].file = fopen(blockFilePath, "wb");
		if (NOT self->branches[0].blockFiles[0].file) {
			free(self->branches[0].blockFiles);
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not open the first block file.");
			return false;
		}
		self->numOpenBlockFiles = 1;
		// Write genesis block data. Begins with length.
		if(fwrite((uint8_t []){
			0x0D,0x11,0x00,0x00, // The Length of the block is 285 bytes or 0x11D
			0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3B,0xA3,0xED,0xFD,0x7A,0x7B,0x12,0xB2,0x7A,0xC7,0x2C,0x3E,0x67,0x76,0x8F,0x61,0x7F,0xC8,0x1B,0xC3,0x88,0x8A,0x51,0x32,0x3A,0x9F,0xB8,0xAA,0x4B,0x1E,0x5E,0x4A,0x29,0xAB,0x5F,0x49,0xFF,0xFF,0x00,0x1D,0x1D,0xAC,0x2B,0x7C,0x01,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x4D,0x04,0xFF,0xFF,0x00,0x1D,0x01,0x04,0x45,0x54,0x68,0x65,0x20,0x54,0x69,0x6D,0x65,0x73,0x20,0x30,0x33,0x2F,0x4A,0x61,0x6E,0x2F,0x32,0x30,0x30,0x39,0x20,0x43,0x68,0x61,0x6E,0x63,0x65,0x6C,0x6C,0x6F,0x72,0x20,0x6F,0x6E,0x20,0x62,0x72,0x69,0x6E,0x6B,0x20,0x6F,0x66,0x20,0x73,0x65,0x63,0x6F,0x6E,0x64,0x20,0x62,0x61,0x69,0x6C,0x6F,0x75,0x74,0x20,0x66,0x6F,0x72,0x20,0x62,0x61,0x6E,0x6B,0x73,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0xF2,0x05,0x2A,0x01,0x00,0x00,0x00,0x43,0x41,0x04,0x67,0x8A,0xFD,0xB0,0xFE,0x55,0x48,0x27,0x19,0x67,0xF1,0xA6,0x71,0x30,0xB7,0x10,0x5C,0xD6,0xA8,0x28,0xE0,0x39,0x09,0xA6,0x79,0x62,0xE0,0xEA,0x1F,0x61,0xDE,0xB6,0x49,0xF6,0xBC,0x3F,0x4C,0xEF,0x38,0xC4,0xF3,0x55,0x04,0xE5,0x1E,0xC1,0x12,0xDE,0x5C,0x38,0x4D,0xF7,0xBA,0x0B,0x8D,0x57,0x8A,0x4C,0x70,0x2B,0x6B,0xF1,0x1D,0x5F,0xAC,0x00,0x00,0x00,0x00}, 1, 289, self->branches[0].blockFiles[0].file) != 289){
			fclose(self->validatorFile);
			remove(validatorFilePath);
			fclose(self->branches[0].blockFiles[0].file);
			free(self->branches[0].blockFiles);
			remove(blockFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not write the genesis block.");
			return false;
		}
		// Write initial validator data
		if(fwrite((uint8_t []){
			0, // Main branch is the first branch
			1, // One branch
			1,0,0,0, // One reference
			0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00, // Genesis hash
			0,0, // Genesis is in first file
			0,0,0,0,0,0,0,0, // Genesis is at position 0
			0,0,0,0, // Last block is genesis at 0
			0x29,0xAB,0x5F,0x49, // Last retarget at the genesis timestamp
			0xFF,0xFF,0x00,0x1D, // Last target is the maximum
			// The genesis timestamp 6 times for the previous times.
			0x29,0xAB,0x5F,0x49,
			0x29,0xAB,0x5F,0x49,
			0x29,0xAB,0x5F,0x49,
			0x29,0xAB,0x5F,0x49,
			0x29,0xAB,0x5F,0x49,
			0x29,0xAB,0x5F,0x49,
			0, // For parent branch. Not used for this branch starting at 0
			0,0,0,0, // Starts at 0
			0,0,0,1, // Validation required on 1.
			1, // One byte for the work.
			0, // No work yet. Genesis work not included since there is no point.
			0,0,0,0, // No unspent outputs.
			0, // No orphans
		}, 1, 120, self->validatorFile) != 96){
			fclose(self->validatorFile);
			remove(validatorFilePath);
			fclose(self->branches[0].blockFiles[0].file);
			free(self->branches[0].blockFiles);
			remove(blockFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			free(self->dataDir);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not write the initial validator information.");
			return false;
		}
		// Flush data
		fflush(self->branches[0].blockFiles[0].file);
		fflush(self->validatorFile);
		free(validatorFilePath);
		free(blockFilePath);
	}
	return true;
}

//  Destructor

void BEFreeFullValidator(void * self){
	CBFreeObject(self);
}

//  Functions

bool BEFullValidatorAddBlockToBranch(BEFullValidator * self, uint8_t branch, CBBlock * block){
	
	return true;
}
bool BEFullValidatorAddBlockToOrphans(BEFullValidator * self, CBBlock * block){
	// Save orphan.
	// Write new orphan number
	fseek(self->validatorFile, self->orphansStart, SEEK_SET);
	if (fwrite(&self->numOrphans, 1, 1, self->validatorFile) != 1)
		return false;
	// Write the block
	for (uint8_t x = 0; x < self->numOrphans; x++)
		fseek(self->validatorFile, CBGetMessage(self->orphans[x])->bytes->length, SEEK_CUR);
	if (fwrite(CBByteArrayGetData(CBGetMessage(block)->bytes), 1, CBGetMessage(block)->bytes->length, self->validatorFile) != CBGetMessage(block)->bytes->length)
		return false;
	// Flush update
	fflush(self->validatorFile);
	// Add to memory
	self->orphans[self->numOrphans] = block;
	CBRetainObject(block);
	return true;
}
BEBlockStatus BEFullValidatorBasicBlockValidation(BEFullValidator * self, CBBlock * block, uint8_t * txHashes, uint64_t networkTime){
	// Get the block hash
	uint8_t * hash = CBBlockGetHash(block);
	for (uint8_t x = 0; x < self->numOrphans; x++)
		if (NOT memcmp(CBBlockGetHash(self->orphans[x]), hash, 32))
			return BE_BLOCK_STATUS_DUPLICATE;
	for (uint8_t x = 0; x < self->numBranches; x++)
		if (BEFullValidatorFindObject((uint8_t *)self->branches[x].references, self->branches[x].numRefs, sizeof(*self->branches[x].references), hash))
			return BE_BLOCK_STATUS_DUPLICATE;
	// Check block has transactions
	if (NOT block->transactionNum)
		return BE_BLOCK_STATUS_BAD;
	// Check block hash against target and that it is below the maximum allowed target.
	if (NOT CBValidateProofOfWork(hash, block->target))
		return BE_BLOCK_STATUS_BAD;
	// Check the block is within two hours of the network time.
	if (block->time > networkTime + 7200)
		return BE_BLOCK_STATUS_BAD_TIME;
	// Calculate merkle root.
	CBCalculateMerkleRoot(txHashes, block->transactionNum);
	// Check merkle root
	int res = memcmp(txHashes, CBByteArrayGetData(block->merkleRoot), 32);
	free(txHashes);
	if (res){
		free(txHashes);
		return BE_BLOCK_STATUS_BAD;
	}
	return BE_BLOCK_STATUS_CONTINUE;
}
BEBlockStatus BEFullValidatorBasicBlockValidationCopy(BEFullValidator * self, CBBlock * block, uint8_t * txHashes, uint64_t networkTime){
	uint8_t * hashes = malloc(block->transactionNum * 32);
	if (NOT hashes) {
		free(txHashes);
		return BE_BLOCK_STATUS_ERROR;
	}
	memcpy(hashes, txHashes, block->transactionNum * 32);
	BEBlockStatus res = BEFullValidatorBasicBlockValidation(self, block, hashes, networkTime);
	free(hashes);
	return res;
}
BEBlockValidationResult BEFullValidatorCompleteBlockValidation(BEFullValidator * self, uint8_t branch, CBBlock * block, uint8_t * txHashes, uint32_t height){
	// Check that the first transaction is a coinbase transaction.
	if (NOT CBTransactionIsCoinBase(block->transactions[0]))
		return BE_BLOCK_VALIDATION_BAD;
	uint64_t blockReward = CBCalculateBlockReward(height);
	uint64_t coinbaseOutputValue;
	uint32_t sigOps = 0;
	// Do validation for transactions.
	bool err;
	CBPrevOut ** allSpentOutputs = malloc(sizeof(*allSpentOutputs) * block->transactionNum);
	for (uint32_t x = 0; x < block->transactionNum; x++) {
		// Check that the transaction is final.
		if (NOT CBTransactionIsFinal(block->transactions[x], block->time, height))
			return BE_BLOCK_VALIDATION_BAD;
		// Do the basic validation
		uint64_t outputValue;
		allSpentOutputs[x] = CBTransactionValidateBasic(block->transactions[x], NOT x, &outputValue, &err);
		if (err){
			for (uint32_t c = 0; c < x; c++)
				free(allSpentOutputs[c]);
			return BE_BLOCK_VALIDATION_ERR;
		}
		if (NOT allSpentOutputs[x]){
			for (uint32_t c = 0; c < x; c++)
				free(allSpentOutputs[c]);
			return BE_BLOCK_VALIDATION_BAD;
		}
		// Check correct structure for coinbase
		if (CBTransactionIsCoinBase(block->transactions[x])){
			if (x)
				return BE_BLOCK_VALIDATION_BAD;
			coinbaseOutputValue = outputValue;
		}else if (NOT x)
			return BE_BLOCK_VALIDATION_BAD;
		// Count sigops
		sigOps += CBTransactionGetSigOps(block->transactions[x]);
		if (sigOps > CB_MAX_SIG_OPS)
			return BE_BLOCK_VALIDATION_BAD;
		// Verify each input and count input values
		uint64_t inputValue = 0;
		for (uint32_t y = 0; y < block->transactions[x]->inputNum; y++) {
			BEBlockValidationResult res = BEFullValidatorInputValidation(self, branch, block, height, x, y, allSpentOutputs, txHashes, &inputValue,&sigOps);
			if (res != BE_BLOCK_VALIDATION_OK) {
				for (uint32_t c = 0; c < x; c++)
					free(allSpentOutputs[c]);
				return res;
			}
		}
		// Done now with the spent outputs
		for (uint32_t c = 0; c < x; c++)
			free(allSpentOutputs[c]);
		// Verify values and add to block reward
		if (inputValue < outputValue)
			return BE_BLOCK_VALIDATION_BAD;
		blockReward += inputValue - outputValue;
	}
	// Verify coinbase output for reward
	if (coinbaseOutputValue > blockReward)
		return BE_BLOCK_VALIDATION_BAD;
	return BE_BLOCK_VALIDATION_OK;
}
FILE * BEFullValidatorGetBlockFile(BEFullValidator * self, uint16_t fileID, uint8_t branch){
	// Look to see if the file descriptor is open. Search using linear search because we are almsot certainly dealing with a low number of files. Modern filesystems can have filesizes in many terabytes to exabytes.... providing you have the storage obviously.
	FILE * fd;
	for (uint16_t x = 0;; x++) {
		if (self->branches[branch].blockFiles->fileID > fileID || x == self->numOpenBlockFiles) {
			// Not in list. Open file.
			char blockFile[strlen(self->dataDir) + 9];
			sprintf(blockFile, "%sblock%i-%i.dat",self->dataDir, branch, fileID);
			fd = fopen(blockFile, "wb+");
			if (NOT fd) {
				// Cannot open file.
				return NULL;
			}
			// Insert new BEBlockFile
			if (self->numOpenBlockFiles == self->fileDescLimit)
				// Cannot fit anymore. Move items down.
				memmove(self->branches[branch].blockFiles, self->branches[branch].blockFiles + 1, x - 1);
			else{
				// Expand list
				self->numOpenBlockFiles++;
				self->branches[branch].blockFiles = realloc(self->branches[branch].blockFiles, sizeof(*self->branches[branch].blockFiles) * self->numOpenBlockFiles);
				if (NOT self->branches[branch].blockFiles) {
					// Unfortunately going to have to cut some off. Chop chop chop
					self->numOpenBlockFiles--;
				}
				if (x != self->numOpenBlockFiles - 1)
					// Move list up unless we are adding to the end.
					memmove(self->branches[branch].blockFiles + x + 1, self->branches[branch].blockFiles + x, self->numOpenBlockFiles - x - 1);
			}
			// Insert
			self->branches[branch].blockFiles[x].fileID = fileID;
			self->branches[branch].blockFiles[x].file = fd;
			return fd;
		}else if (self->branches[branch].blockFiles[x].fileID == fileID){
			// Got it
			fd = self->branches[branch].blockFiles[x].file;
			return fd;
		}
	}
}
BEBlockValidationResult BEFullValidatorInputValidation(BEFullValidator * self, uint8_t branch, CBBlock * block, uint32_t blockHeight, uint32_t transactionIndex,uint32_t inputIndex, CBPrevOut ** allSpentOutputs, uint8_t * txHashes, uint64_t * value, uint32_t * sigOps){
	// Check that the previous output is not already spent by this block.
	for (uint32_t a = 0; a < transactionIndex; a++)
		for (uint32_t b = 0; b < block->transactions[a]->inputNum; b++)
			if (CBByteArrayCompare(allSpentOutputs[transactionIndex][inputIndex].hash, allSpentOutputs[a][b].hash)
				&& allSpentOutputs[transactionIndex][inputIndex].index == allSpentOutputs[a][b].index)
				// Duplicate found.
				return BE_BLOCK_VALIDATION_BAD;
	// Now we need to check that the output is in this block (before this transaction) or unspent elsewhere in the blockchain.
	CBTransactionOutput * prevOut;
	bool found = false;
	for (uint32_t a = 0; a < transactionIndex; a++) {
		if (NOT memcmp(txHashes + 32*a, CBByteArrayGetData(allSpentOutputs[transactionIndex][inputIndex].hash), 32)) {
			// This is the transaction hash. Make sure there is the output.
			if (block->transactions[a]->outputNum <= allSpentOutputs[transactionIndex][inputIndex].index)
				// Too few outputs.
				return BE_BLOCK_VALIDATION_BAD;
			prevOut = block->transactions[a]->outputs[allSpentOutputs[transactionIndex][inputIndex].index];
			found = true;
			break;
		}
	}
	if (NOT found) {
		// Not found in this block. Look in unspent outputs index.
		BEOutputReference * outRef = BEFullValidatorFindObject((uint8_t *) self->branches[branch].unspentOutputs, self->branches[branch].numUnspentOutputs, sizeof(*self->branches[branch].unspentOutputs),CBByteArrayGetData(allSpentOutputs[transactionIndex][inputIndex].hash));
		if (NOT outRef)
			// No unspent outputs for this input.
			return BE_BLOCK_VALIDATION_BAD;
		// Check coinbase maturity
		if (outRef->coinbase && outRef->height < blockHeight - CB_COINBASE_MATURITY)
			return BE_BLOCK_VALIDATION_BAD;
		// Get the output
		FILE * fd = BEFullValidatorGetBlockFile(self, outRef->ref.fileID,outRef->branch);
		if (NOT fd)
			return BE_BLOCK_VALIDATION_ERR;
		// Now load the output. Do deserilisation here as we do not know the number of bytes required.
		uint8_t bytes[9];
		fseek(fd, outRef->ref.filePos, SEEK_SET);
		if (fread(bytes, 1, 9, fd) != 9)
			return BE_BLOCK_VALIDATION_ERR;
		fseek(fd, 9, SEEK_CUR);
		uint32_t scriptSize;
		if (bytes[8] < 253)
			scriptSize = bytes[8];
		else{
			uint8_t varIntSize;
			if (bytes[8] == 253)
				varIntSize = 2;
			else if (bytes[8] == 254)
				varIntSize = 4;
			else
				varIntSize = 8;
			// Read the script size
			uint8_t scriptSizeBytes[varIntSize];
			if (fread(scriptSizeBytes, 1, varIntSize, fd) != varIntSize)
				return BE_BLOCK_VALIDATION_ERR;
			// Seek to script
			fseek(fd, 9, SEEK_CUR);
			for (uint8_t y = 0; y < varIntSize; y++)
				scriptSize = scriptSizeBytes[y] << 8*y;
		}
		// Get script
		CBScript * script = CBNewScriptOfSize(scriptSize, self->onErrorReceived);
		if (fread(CBByteArrayGetData(script), 1, scriptSize, fd) != scriptSize)
			return BE_BLOCK_VALIDATION_ERR;
		// Make output
		prevOut = CBNewTransactionOutput(bytes[0] | (uint64_t)bytes[1] << 8 | (uint64_t)bytes[2] << 16 | (uint64_t)bytes[3] << 24 | (uint64_t)bytes[4] << 32 | (uint64_t)bytes[5] << 40 | (uint64_t)bytes[6] << 48 | (uint64_t)bytes[7] << 56, script, self->onErrorReceived);
		CBReleaseObject(script);
	}
	// We have sucessfully received an output for this input. Verify the input script for the output script.
	CBScriptStack stack = CBNewEmptyScriptStack();
	// Execute the input script.
	CBScriptExecuteReturn res = CBScriptExecute(block->transactions[transactionIndex]->inputs[inputIndex]->scriptObject, &stack, CBTransactionGetInputHashForSignature, block->transactions[transactionIndex], inputIndex, false);
	if (res == CB_SCRIPT_ERR){
		CBFreeScriptStack(stack);
		CBReleaseObject(prevOut);
		return BE_BLOCK_VALIDATION_ERR;
	}
	if (res == CB_SCRIPT_INVALID){
		CBFreeScriptStack(stack);
		CBReleaseObject(prevOut);
		return BE_BLOCK_VALIDATION_BAD;
	}
	// Verify P2SH inputs.
	if (CBScriptIsP2SH(prevOut->scriptObject)){
		if (NOT CBScriptIsPushOnly(prevOut->scriptObject)){
			CBFreeScriptStack(stack);
			CBReleaseObject(prevOut);
			return BE_BLOCK_VALIDATION_BAD;
		}
		// Since the output is a P2SH we include the serialised script in the signature operations
		CBScript * p2shScript = CBNewScriptWithData(stack.elements[stack.length - 1].data, stack.elements[stack.length - 1].length, self->onErrorReceived);
		*sigOps += CBScriptGetSigOpCount(p2shScript, true);
		if (*sigOps > CB_MAX_SIG_OPS){
			CBFreeScriptStack(stack);
			CBReleaseObject(prevOut);
			return BE_BLOCK_VALIDATION_BAD;
		}
		CBReleaseObject(p2shScript);
	}
	// Execute the output script.
	res = CBScriptExecute(prevOut->scriptObject, &stack, CBTransactionGetInputHashForSignature, block->transactions[transactionIndex], inputIndex, true);
	// Finished with the stack.
	CBFreeScriptStack(stack);
	// Increment the value with the input value then be done with the output
	*value += prevOut->value;
	CBReleaseObject(prevOut);
	// Check the result of the output script
	if (res == CB_SCRIPT_ERR)
		return BE_BLOCK_VALIDATION_ERR;
	if (res == CB_SCRIPT_INVALID)
		return BE_BLOCK_VALIDATION_BAD;
	return BE_BLOCK_VALIDATION_OK;
}
void * BEFullValidatorFindObject(uint8_t * objects, uint32_t objectNum, size_t offset, uint8_t * hash){
	// Block branch block reference lists and the unspent output reference list, use sorted lists, therefore this uses an interpolation search which is an optimsation on binary search.
	uint32_t left = 0;
	uint32_t right = objectNum - 1;
	uint64_t miniKey = BEHashMiniKey(hash);
	uint64_t leftMiniKey;
	uint64_t rightMiniKey;
	for (;;) {
		uint8_t * leftHash = objects + left*offset;
		uint8_t * rightHash = objects + right*offset;
		if (memcmp(hash, rightHash, 32) > 0
			|| memcmp(hash, leftHash, 32) < 0)
			return NULL;
		uint32_t pos;
		if (rightMiniKey - leftMiniKey){
			leftMiniKey = BEHashMiniKey(leftHash);
			rightMiniKey = BEHashMiniKey(rightHash);
			if (rightMiniKey - leftMiniKey)
				pos = left + (uint32_t)( (right - left) * (miniKey - leftMiniKey) ) / (rightMiniKey - leftMiniKey);
			else
				pos = pos = (right + left)/2;
		}else
			pos = pos = (right + left)/2;
		int res = memcmp(hash, objects + pos*offset, 32);
		if (NOT res)
			return objects + pos*offset;
		else if (res > 0)
			left = pos + 1;
		else
			right = pos - 1;
	}
}
CBBlock * BEFullValidatorLoadBlock(BEFullValidator * self, BEBlockReference blockRef){
	// Get the file
	FILE * fd = BEFullValidatorGetBlockFile(self, blockRef.ref.fileID, blockRef.branch);
	if (NOT fd)
		return NULL;
	// Now we have the file and it is open we need the block. Get the length of the block.
	uint8_t length[4];
	fseek(fd, blockRef.ref.filePos, SEEK_SET);
	if (fread(length, 1, 4, fd) != 4)
		return NULL;
	CBByteArray * data = CBNewByteArrayOfSize(length[3] << 24 | length[2] << 16 | length[1] << 8 | length[0], self->onErrorReceived);
	// Now read block data
	fseek(fd, 4, SEEK_CUR);
	if (fread(CBByteArrayGetData(data), 1, data->length, fd) != data->length) {
		CBReleaseObject(data);
		return NULL;
	}
	// Make and return the block
	CBBlock * block = CBNewBlockFromData(data, self->onErrorReceived);
	CBReleaseObject(data);
	return block;
}
BEBlockStatus BEFullValidatorProcessBlock(BEFullValidator * self, CBBlock * block, uint64_t networkTime){
	// Get transaction hashes.
	uint8_t * txHashes = malloc(32 * block->transactionNum);
	if (NOT txHashes)
		return BE_BLOCK_STATUS_ERROR;
	// Put the hashes for transactions into a list.
	for (uint32_t x = 0; x < block->transactionNum; x++)
		memcpy(txHashes + 32*x, CBTransactionGetHash(block->transactions[x]), 32);
	// Determine what type of block this is.
	uint8_t x = 0;
	uint32_t height; // To be set to the height of the block being added.
	BEBlockReference * ref;
	for (; x < self->numBranches; x++){
		ref = BEFullValidatorFindObject((uint8_t *)self->branches[x].references, self->branches[x].numRefs, sizeof(*self->branches[x].references), CBByteArrayGetData(block->prevBlockHash));
		if (ref){
			// The block is extending this branch or creating a side branch to this branch
			height = ref->height;
			break;
		}
	}
	if (x == self->numBranches){
		// Orphan block. End here.
		if (self->numOrphans == BE_MAX_ORPHAN_CACHE)
			return BE_BLOCK_STATUS_MAX_CACHE;
		// Do basic validation
		BEBlockStatus res = BEFullValidatorBasicBlockValidation(self, block, txHashes, networkTime);
		free(txHashes);
		if (res != BE_BLOCK_STATUS_CONTINUE)
			return res;
		// Add block to orphans
		if(BEFullValidatorAddBlockToOrphans(self,block))
			return BE_BLOCK_STATUS_ORPHAN;
		return BE_BLOCK_STATUS_ERROR;
	}
	// Not an orphan. See if this is an extention or new branch.
	BEBlockBranch * branch;
	if (height == self->branches[x].references[self->branches[x].lastBlock].height + 1) {
		// Extension
		// Do basic validation with a copy of the transaction hashes.
		BEBlockStatus res = BEFullValidatorBasicBlockValidationCopy(self, block, txHashes, networkTime);
		if (res != BE_BLOCK_STATUS_CONTINUE){
			free(txHashes);
			return res;
		}
		branch = self->branches + x;
	}else{
		// New branch
		if (self->numBranches == BE_MAX_BRANCH_CACHE) {
			// No more branches allowed.
			free(txHashes);
			return BE_BLOCK_STATUS_MAX_CACHE;
		}
		// Do basic validation with a copy of the transaction hashes.
		BEBlockStatus res = BEFullValidatorBasicBlockValidationCopy(self, block, txHashes, networkTime);
		if (res != BE_BLOCK_STATUS_CONTINUE){
			free(txHashes);
			return res;
		}
		branch = self->branches + self->numBranches;
		// Initialise minimal data the new branch.
		// Record parent branch.
		branch->parentBranch = x;
		x = self->numBranches;
		// Detemine last retarget.
		branch->lastTarget = ref->target;
		uint32_t lastRetargetOffset = ref->height % 2016;
		// Add previous times.
		for (uint32_t y = 0; y < BE_MAX(lastRetargetOffset,6); y++) {
			if (y < 7)
				branch->prevTime[6-y] = ref->time;
			if (ref->height)
				ref = self->branches[ref->prevBranch].references + ref->prevIndex;
		}
		// Set retarget time
		branch->lastRetargetTime = ref->time;
		// Calculate the work
		ref = self->branches[branch->parentBranch].references + self->branches[branch->parentBranch].lastBlock;
		branch->work = self->branches[branch->parentBranch].work;
		for (uint32_t y = 0; y < ref->height - height + 1; y++) {
			CBBigInt tempWork = CBCalculateBlockWork(ref->target);
			CBBigIntEqualsSubtractionByBigInt(&branch->work, &tempWork);
			free(tempWork.data);
			ref = ref = self->branches[ref->prevBranch].references + ref->prevIndex;
		}
	}
	// Check timestamp
	if (block->time < branch->prevTime[0]){
		free(txHashes);
		return BE_BLOCK_STATUS_BAD;
	}
	uint32_t target;
	if (NOT ((ref->height + 1) % 2016))
		// Difficulty change for this branch
		target = CBCalculateTarget(branch->lastTarget, block->time - branch->lastRetargetTime);
	else
		target = branch->lastTarget;
	// Check target
	if (block->target != branch->lastTarget){
		free(txHashes);
		return BE_BLOCK_STATUS_BAD;
	}
	// Calculate total work
	CBBigInt work = CBCalculateBlockWork(block->target);
	CBBigIntEqualsAdditionByBigInt(&work, &branch->work);
	if (x != self->mainBranch) {
		// Check if the block is adding to a side branch without becoming the main branch
		if (CBBigIntCompareToBigInt(work,self->branches[self->mainBranch].work) != CB_COMPARE_MORE_THAN){
			free(txHashes);
			// Add to branch without complete validation
			
			return BE_BLOCK_STATUS_SIDE;
		}
		// Potential block-chain reorganisation. Validate the side branch.
		for (uint32_t y = self->branches[x].startValidation; y < self->branches[x].numRefs; y++) {
			BEBlockValidationResult res = BEFullValidatorCompleteBlockValidation(self, x, block, txHashes, self->branches[x].startHeight + self->branches[x].numRefs);
			free(txHashes);
			if (res == BE_BLOCK_VALIDATION_BAD)
				return BE_BLOCK_STATUS_BAD;
			if (res == BE_BLOCK_VALIDATION_ERR)
				return BE_BLOCK_STATUS_ERROR;
			// Add to branch
			
		}
	}else{
		// We are just validating a new block on the main chain
		BEBlockValidationResult res = BEFullValidatorCompleteBlockValidation(self, x, block, txHashes, self->branches[x].startHeight + self->branches[x].numRefs);
		free(txHashes);
		switch (res) {
			case BE_BLOCK_VALIDATION_BAD:
				return BE_BLOCK_STATUS_BAD;
				break;
			case BE_BLOCK_VALIDATION_ERR:
				return BE_BLOCK_STATUS_ERROR;
				break;
			case BE_BLOCK_VALIDATION_OK:
				// Update branch and unspent outputs.
				
				break;
		}
	}
}
