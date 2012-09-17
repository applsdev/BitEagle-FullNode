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
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create buffer of size %u.",fileLen);
			return false;
		}
		size_t res = fread(CBByteArrayGetData(buffer), 1, fileLen, self->validatorFile);
		if(res != fileLen){
			CBReleaseObject(buffer);
			fclose(self->validatorFile);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not read %u bytes of data into buffer. fread returned %u",fileLen,res);
			return false;
		}
		// Deserailise data
		if (buffer->length >= 77){
			self->numOrphans = CBByteArrayGetByte(buffer, 0);
			if (buffer->length >= 77 + self->numOrphans*42){
				uint32_t cursor = 1;
				for (uint8_t x = 0; x < self->numOrphans; x++) {
					memcpy(self->orphans[x].blockHash, CBByteArrayGetData(buffer) + cursor, 32);
					cursor += 32;
					self->orphans[x].ref.fileID = CBByteArrayReadInt16(buffer, cursor);
					cursor += 2;
					self->orphans[x].ref.filePos = CBByteArrayReadInt64(buffer, cursor);
					cursor += 8;
				}
				if (buffer->length >= cursor + 76) {
					self->mainBranch = CBByteArrayGetByte(buffer, cursor);
					cursor++;
					self->numBranches = CBByteArrayGetByte(buffer, cursor);
					cursor++;
					if (self->numBranches && self->numBranches <= BE_MAX_BRANCH_CACHE) {
						bool err = false;
						uint8_t x = 0;
						for (; x < self->numBranches; x++) {
							if (buffer->length < cursor + 74){
								err = true;
								onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the number of references %u < %u",buffer->length, cursor + 74);
								break;
							}
							self->branches[x].numRefs = CBByteArrayReadInt32(buffer, cursor);
							if (buffer->length < cursor + 70 + self->branches[x].numRefs*42) {
								err = true;
								onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the references %u < %u",buffer->length, cursor + 70 + self->branches[x].numRefs*42);
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
							}
							if (buffer->length < cursor + 70) {
								err = true;
								onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the remaining branch %u < %u",buffer->length, cursor + 70);
								break;
							}
							self->branches[x].lastBlock = CBByteArrayReadInt32(buffer, cursor);
							cursor += 4;
							self->branches[x].lastRetarget = CBByteArrayReadInt32(buffer, cursor);
							cursor += 4;
							self->branches[x].lastTarget = CBByteArrayReadInt32(buffer, cursor);
							cursor += 4;
							for (uint8_t x = 0; x < 6; x++) {
								self->branches[x].prevTime[x] = CBByteArrayReadInt64(buffer, cursor);
								cursor += 8;
							}
							self->branches[x].parentBranch = CBByteArrayGetByte(buffer, cursor);
							cursor++;
							self->branches[x].startHeight = CBByteArrayReadInt32(buffer, cursor);
							cursor+= 4;
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
						if (NOT err) {
							self->numUnspentOutputs = CBByteArrayReadInt32(buffer, cursor);
							cursor += 4;
							if (buffer->length >= cursor + self->numUnspentOutputs*46) {
								if (self->numUnspentOutputs) {
									self->unspentOutputs = malloc(sizeof(*self->unspentOutputs) * self->numUnspentOutputs);
									if (NOT self->unspentOutputs) {
										// Free branch data
										for (uint8_t x = 0; x < self->numBranches; x++)
											free(self->branches[x].references);
										CBReleaseObject(buffer);
										fclose(self->validatorFile);
										onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for unspent outputs in BEInitFullValidator.",sizeof(*self->unspentOutputs) * self->numUnspentOutputs);
										return false;
									}
									for (uint32_t x = 0; x < self->numUnspentOutputs; x++) {
										memcpy(self->unspentOutputs[x].outputHash, CBByteArrayGetData(buffer) + cursor, 32);
										cursor += 32;
										self->unspentOutputs[x].outputIndex = CBByteArrayReadInt32(buffer, cursor);
										cursor += 4;
										self->unspentOutputs[x].ref.fileID = CBByteArrayReadInt16(buffer, cursor);
										cursor += 2;
										self->unspentOutputs[x].ref.filePos = CBByteArrayReadInt64(buffer, cursor);
										cursor += 8;
									}
								}
								// Success
								CBReleaseObject(buffer);
								// All done
								return true;
							}
						}else{
							// Free branch data
							for (uint8_t y = 0; y < x; y++)
								free(self->branches[y].references);
						}
					}else
						onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"The number of branches is invalid.");
				}else
					onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the information after the orphans %u < %u",buffer->length, cursor + 76);
			}else
				onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the orphans %u < %u",buffer->length, 77 + self->numOrphans*42);
		}else
			onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for anything %u < 77",buffer->length);
		CBReleaseObject(buffer);
		fclose(self->validatorFile);
		return false;
	}else{
		// Create initial data
		// Create directory if it doesn't already exist
		if(mkdir(dataDir, S_IREAD | S_IWRITE))
			if (errno != EEXIST){
				free(validatorFilePath);
				onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create the data directory.");
				return false;
			}
		// Open validator file
		self->validatorFile = fopen(validatorFilePath, "wb");
		if (NOT self->validatorFile){
			free(validatorFilePath);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not open the validator file.");
			return false;
		}
		self->numOrphans = 0;
		self->numUnspentOutputs = 0;
		self->unspentOutputs = NULL;
		// Make branch with genesis block
		self->numBranches = 1;
		self->branches[0].lastBlock = 0;
		self->branches[0].lastRetarget = 0;
		self->branches[0].lastTarget = CB_MAX_TARGET;
		for (uint8_t x = 0; x < 6; x++)
			self->branches[0].prevTime[x] = 1231006505; // Genesis block time
		self->branches[0].startHeight = 0;
		self->branches[0].numRefs = 1;
		self->branches[0].references = malloc(sizeof(*self->branches[0].references));
		if (NOT self->branches[0].references) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the genesis reference in BEInitFullValidator.",sizeof(*self->branches[0].references));
			return false;
		}
		uint8_t genesisHash[32] = {0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00};
		memcpy(self->branches[0].references[0].blockHash,genesisHash,32);
		self->branches[0].references[0].ref.fileID = 0;
		self->branches[0].references[0].ref.filePos = 0;
		self->branches[0].work.length = 1;
		self->branches[0].work.data = malloc(1);
		if (NOT self->branches[0].work.data) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
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
			onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the first block file path in BEInitFullValidator.",dataDirLen + 12);
			return false;
		}
		memcpy(blockFilePath, dataDir, dataDirLen);
		strcpy(blockFilePath + dataDirLen, "blocks0.dat");
		self->blockFiles = malloc(sizeof(*self->blockFiles));
		if (NOT self->blockFiles) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the blockFiles list in BEInitFullValidator.",sizeof(*self->blockFiles));
			return false;
		}
		self->blockFiles[0].fileID = 0;
		self->blockFiles[0].file = fopen(blockFilePath, "wb");
		if (NOT self->blockFiles[0].file) {
			fclose(self->validatorFile);
			remove(validatorFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not open the first block file.");
			return false;
		}
		self->numOpenBlockFiles = 1;
		// Write genesis block data
		if(fwrite((uint8_t []){0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3B,0xA3,0xED,0xFD,0x7A,0x7B,0x12,0xB2,0x7A,0xC7,0x2C,0x3E,0x67,0x76,0x8F,0x61,0x7F,0xC8,0x1B,0xC3,0x88,0x8A,0x51,0x32,0x3A,0x9F,0xB8,0xAA,0x4B,0x1E,0x5E,0x4A,0x29,0xAB,0x5F,0x49,0xFF,0xFF,0x00,0x1D,0x1D,0xAC,0x2B,0x7C,0x01,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x4D,0x04,0xFF,0xFF,0x00,0x1D,0x01,0x04,0x45,0x54,0x68,0x65,0x20,0x54,0x69,0x6D,0x65,0x73,0x20,0x30,0x33,0x2F,0x4A,0x61,0x6E,0x2F,0x32,0x30,0x30,0x39,0x20,0x43,0x68,0x61,0x6E,0x63,0x65,0x6C,0x6C,0x6F,0x72,0x20,0x6F,0x6E,0x20,0x62,0x72,0x69,0x6E,0x6B,0x20,0x6F,0x66,0x20,0x73,0x65,0x63,0x6F,0x6E,0x64,0x20,0x62,0x61,0x69,0x6C,0x6F,0x75,0x74,0x20,0x66,0x6F,0x72,0x20,0x62,0x61,0x6E,0x6B,0x73,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0xF2,0x05,0x2A,0x01,0x00,0x00,0x00,0x43,0x41,0x04,0x67,0x8A,0xFD,0xB0,0xFE,0x55,0x48,0x27,0x19,0x67,0xF1,0xA6,0x71,0x30,0xB7,0x10,0x5C,0xD6,0xA8,0x28,0xE0,0x39,0x09,0xA6,0x79,0x62,0xE0,0xEA,0x1F,0x61,0xDE,0xB6,0x49,0xF6,0xBC,0x3F,0x4C,0xEF,0x38,0xC4,0xF3,0x55,0x04,0xE5,0x1E,0xC1,0x12,0xDE,0x5C,0x38,0x4D,0xF7,0xBA,0x0B,0x8D,0x57,0x8A,0x4C,0x70,0x2B,0x6B,0xF1,0x1D,0x5F,0xAC,0x00,0x00,0x00,0x00}, 1, 285, self->blockFiles[0].file) != 285){
			fclose(self->validatorFile);
			remove(validatorFilePath);
			fclose(self->blockFiles[0].file);
			remove(blockFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not write the genesis block.");
			return false;
		}
		// Write initial validator data
		if(fwrite((uint8_t []){
			0, // No orphans
			0, // Main branch is the first branch
			1, // One branch
			1,0,0,0, // One reference
			0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00, // Genesis hash
			0,0, // Genesis is in first file
			0,0,0,0,0,0,0,0, // Genesis is at position 0
			0,0,0,0, // Last block is genesis at 0
			0,0,0,0, // Last retarget at 0
			0xFF,0xFF,0x00,0x1D, // Last target is the maximum
			// The genesis timestamp 6 times for the previous times.
			0x29,0xAB,0x5F,0x49,0,0,0,0,
			0x29,0xAB,0x5F,0x49,0,0,0,0,
			0x29,0xAB,0x5F,0x49,0,0,0,0,
			0x29,0xAB,0x5F,0x49,0,0,0,0,
			0x29,0xAB,0x5F,0x49,0,0,0,0,
			0x29,0xAB,0x5F,0x49,0,0,0,0,
			0, // For parent branch. Not used for this branch starting at 0
			0,0,0,0, // Starts at 0
			1, // One byte for the work.
			0, // No work yet. Genesis work not included since there is no point.
			0,0,0,0, // No unspent outputs.
		}, 1, 120, self->validatorFile) != 120){
			fclose(self->validatorFile);
			remove(validatorFilePath);
			fclose(self->blockFiles[0].file);
			remove(blockFilePath);
			free(validatorFilePath);
			free(blockFilePath);
			onErrorReceived(CB_ERROR_INIT_FAIL,"Could not write the initial validator information.");
			return false;
		}
		// Flush data
		fflush(self->blockFiles[0].file);
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

BEBlockReference * BEBlockBranchFindBlock(BEBlockBranch * branch, uint8_t * hash){
	// Branches use a sorted list of block hashes, therefore this uses an interpolation search which is an optimsation on binary search.
	uint32_t left = 0;
	uint32_t right = branch->numRefs - 1;
	uint64_t miniKey = BEHashMiniKey(hash);
	uint64_t leftMiniKey;
	uint64_t rightMiniKey;
	for (;;) {
		if (memcmp(hash, branch->references[right].blockHash, 32) > 0
			|| memcmp(hash, branch->references[left].blockHash, 32) < 0)
			return NULL;
		uint32_t pos;
		if (rightMiniKey - leftMiniKey){
			leftMiniKey = BEHashMiniKey(branch->references[left].blockHash);
			rightMiniKey = BEHashMiniKey(branch->references[right].blockHash);
			if (rightMiniKey - leftMiniKey)
				pos = left + (uint32_t)( (right - left) * (miniKey - leftMiniKey) ) / (rightMiniKey - leftMiniKey);
			else
				pos = pos = (right + left)/2;
		}else
			pos = pos = (right + left)/2;
		int res = memcmp(hash, branch->references[pos].blockHash, 32);
		if (NOT res)
			return branch->references + pos;
		else if (res > 0)
			left = pos + 1;
		else
			right = pos - 1;
	}
}
BEBlockStatus BEFullValidatorProcessBlock(BEFullValidator * self, CBBlock * block, uint64_t networkTime){
	// Get the block hash
	uint8_t * hash = CBBlockGetHash(block);
	for (uint8_t x = 0; x < self->numOrphans; x++)
		if (NOT memcmp(self->orphans[x].blockHash, hash, 32))
			return BE_BLOCK_STATUS_DUPLICATE;
	for (uint8_t x = 0; x < self->numBranches; x++)
		if (BEBlockBranchFindBlock(self->branches + x, hash))
			return BE_BLOCK_STATUS_DUPLICATE;
	// Check block hash against target and that it is below the maximum allowed target.
	if (NOT CBValidateProofOfWork(hash, block->target))
		return BE_BLOCK_STATUS_BAD;
	// Check the block is within two hours of the network time.
	if (block->time > networkTime + 7200)
		return BE_BLOCK_STATUS_BAD_TIME;
	// Verify the transaction data via the merkle root.
	uint8_t * hashes = malloc(32 * block->transactionNum);
	if (NOT hashes)
		return BE_BLOCK_STATUS_ERROR;
	// Put the hashes for transactions into a list.
	for (uint32_t x = 0; x < block->transactionNum; x++)
		memcpy(hashes + 32*x, CBTransactionGetHash(block->transactions[x]), 32);
	// Calculate merkle root.
	CBCalculateMerkleRoot(hashes, block->transactionNum);
	// Check merkle root
	if (memcmp(hashes, CBByteArrayGetData(block->merkleRoot), 32))
		return BE_BLOCK_STATUS_BAD;
	// Determine what type of block this is.
	uint8_t x = 0;
	for (; x < self->numBranches; x++)
		if (NOT memcmp(self->branches[x].references[self->branches[x].lastBlock].blockHash, CBByteArrayGetData(block->prevBlockHash), 32))
			// The block is extending this branch.
			break;
	if (x == self->numBranches)
		// Orphan block. End here.
		return BE_BLOCK_STATUS_ORPHAN;
	// Not an orphan, process for branch
	BEBlockBranch * branch = self->branches + x;
	if (NOT ((branch->startHeight + branch->numRefs) % 2016)) {
		// Difficulty change for this branch
		
	}
	if (x == self->mainBranch){
		// Extending main branch
		
	}else{
		// Extending side branch
		
	}
}
