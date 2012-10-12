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
	// Get maximum file size
	struct rlimit fileLim;
	if(getrlimit(RLIMIT_FSIZE,&fileLim)){
		self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not get RLIMIT_FSIZE limits.");
		return false;
	}
	self->fileSizeLimit = fileLim.rlim_cur;
	// Get the maximum number of allowed files
	if(getrlimit(RLIMIT_NOFILE,&fileLim)){
		self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not get RLIMIT_NOFILE limits.");
		return false;
	}
	// Ensure highest limit. If the OS does not support a high limit, the program may fail. ??? Should code be introduced to close files when limits have been reached? I do not think so because the number of open files is reasonably small and file limits should be increased on the OS size if rlim_max is too small. If it cannot be increased to a reasonable level then the operating system is simply shit.
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
	self->validatorFile = NULL;
	return true;
}

//  Destructor

void BEFreeFullValidator(void * self){
	CBFreeObject(self);
}

//  Functions

bool BEFullValidatorAddBlockToBranch(BEFullValidator * self, uint8_t branch, CBBlock * block){
	// Save block. First find first block file with space.
	uint16_t fileIndex = 0;
	uint64_t size;
	for (; fileIndex < self->branches[branch].numBlockFiles; fileIndex++) {
		// Use stat.h for fast information
		char blockFile[strlen(self->dataDir) + 22];
		sprintf(blockFile, "%sblocks%u-%u.dat",self->dataDir, branch, fileIndex);
		struct stat st;
		if(stat(blockFile, &st))
			return false;
		size = st.st_size;
		if (CBGetMessage(block)->bytes->length + 4 <= self->fileSizeLimit - size)
			// Enough room in this file
			break;
	}
	FILE * fp = BEFullValidatorGetBlockFile(self, fileIndex, branch);
	if (NOT fp)
		return false;
	// Write block to file
	fseek(fp, size, SEEK_SET);
	// Write length
	uint8_t len[4];
	len[0] = CBGetMessage(block)->bytes->length;
	len[1] = CBGetMessage(block)->bytes->length >> 8;
	len[2] = CBGetMessage(block)->bytes->length >> 16;
	len[3] = CBGetMessage(block)->bytes->length >> 24;
	if (fwrite(len, 1, 4, fp) != 4)
		return false;
	// Write block data
	if (fwrite(CBByteArrayGetData(CBGetMessage(block)->bytes), 1, CBGetMessage(block)->bytes->length, fp) != CBGetMessage(block)->bytes->length)
		return false;
	// Modify validator information. Insert new reference. This involves adding the reference to the end of the refence data and inserting an index into a lookup table.
	bool found;
	// Get the index position for the lookup table.
	uint32_t indexPos = BEFullValidatorFindBlockReference(self->branches[branch].referenceTable, self->branches[branch].numRefs, CBBlockGetHash(block), &found);
	// Get the index of the block reference and increase the number of references.
	uint32_t refIndex = self->branches[branch].numRefs++;
	// Reallocate memory for the references
	BEBlockReference * temp = realloc(self->branches[branch].references, sizeof(*self->branches[branch].references) * self->branches[branch].numRefs);
	if (NOT temp) {
		// Failure, reset data
		self->branches[branch].numRefs--;
		ftruncate(fileno(fp), size);
		return false;
	}
	self->branches[branch].references = temp;
	// Reallocate memory for the lookup table
	BEBlockReferenceHashIndex * temp2 = realloc(self->branches[branch].referenceTable, sizeof(*self->branches[branch].referenceTable) * self->branches[branch].numRefs);
	if (NOT temp2) {
		// Failure, reset data
		self->branches[branch].numRefs--;
		ftruncate(fileno(fp), size);
		return false;
	}
	self->branches[branch].referenceTable = temp2;
	// Before we insert the reference, adjust size of unspent output information and make sure that is OK.
	uint32_t temp3 = self->branches[branch].numUnspentOutputs;
	for (uint32_t x = 0; x < block->transactionNum; x++){
		// For each input, an output has been spent but only do this for non-coinbase transactions sicne coinbase transactions make new coins.
		if (x)
			temp3 -= block->transactions[x]->inputNum;
		// For each output, we have another unspent output.
		temp3 += block->transactions[x]->outputNum;
	}
	// Reallocate for new size
	BEOutputReference * temp4 = realloc(self->branches[branch].unspentOutputs, temp3);
	if (NOT temp4) {
		// Failure, reset data
		ftruncate(fileno(fp), size);
		return false;
	}
	self->branches[branch].unspentOutputs = temp4;
	// Now insert reference index into lookup table
	if (indexPos < self->branches[branch].numRefs - 1)
		// Move references up
		memmove(self->branches[branch].referenceTable + indexPos + 1, self->branches[branch].referenceTable + indexPos, sizeof(*self->branches[branch].referenceTable) * (self->branches[branch].numRefs - indexPos - 1));
	self->branches[branch].referenceTable[indexPos].index = refIndex;
	memcpy(self->branches[branch].referenceTable[indexPos].blockHash,CBBlockGetHash(block), 32);
	// Update branch data
	if (NOT (self->branches[branch].startHeight + self->branches[branch].numRefs) % 2016)
		self->branches[branch].lastRetargetTime = block->time;
	// Insert block data
	self->branches[branch].references[refIndex].ref.fileID = fileIndex;
	self->branches[branch].references[refIndex].ref.filePos = size;
	self->branches[branch].references[refIndex].target = block->target;
	self->branches[branch].references[refIndex].time = block->time;
	// Update unspent outputs... Go through transactions, removing the prevOut references and adding the outputs for one transaction at a time.
	uint32_t cursor = 80; // Cursor to find output positions.
	uint8_t byte = CBByteArrayGetByte(CBGetMessage(block)->bytes, 80);
	cursor += byte < 253 ? 1 : (byte == 253 ? 2 : (byte == 254 ? 4 : 8));
	for (uint32_t x = 0; x < block->transactionNum; x++) {
		bool found;
		cursor += 8; // Move along version number and input number.
		// First remove output references than add new outputs.
		for (uint32_t y = 0; y < block->transactions[x]->inputNum; y++) {
			if (x) {
				// Only remove for non-coinbase transactions
				uint32_t ref = BEFullValidatorFindOutputReference(self->branches[branch].unspentOutputs, self->branches[branch].numUnspentOutputs, CBByteArrayGetData(block->transactions[x]->inputs[y]->prevOut.hash), block->transactions[x]->inputs[y]->prevOut.index, &found);
				// Remove by overwrite.
				memmove(self->branches[branch].unspentOutputs + ref, self->branches[branch].unspentOutputs + ref + 1, (self->branches[branch].numUnspentOutputs - ref - 1) * sizeof(*self->branches[branch].unspentOutputs));
				self->branches[branch].numUnspentOutputs--;
			}
			// Move cursor along script varint. We look at byte data in case it is longer than needed.
			uint8_t byte = CBByteArrayGetByte(CBGetMessage(block)->bytes, cursor);
			cursor += byte < 253 ? 1 : (byte == 253 ? 2 : (byte == 254 ? 4 : 8));
			// Move along script and output reference
			cursor += block->transactions[x]->inputs[y]->scriptObject->length + 36;
			// Move cursor along sequence
			cursor += 4;
		}
		// Move cursor past output number to first output
		cursor += 4;
		// Now add new outputs
		for (uint32_t y = 0; y < block->transactions[x]->outputNum; y++) {
			uint32_t ref = BEFullValidatorFindOutputReference(self->branches[branch].unspentOutputs, self->branches[branch].numUnspentOutputs, CBTransactionGetHash(block->transactions[x]), y, &found);
			// Insert the output information at the reference point.
			memmove(self->branches[branch].unspentOutputs + ref + 1, self->branches[branch].unspentOutputs + ref, (self->branches[branch].numUnspentOutputs - ref - 1) * sizeof(*self->branches[branch].unspentOutputs));
			self->branches[branch].numUnspentOutputs++;
			self->branches[branch].unspentOutputs[ref].branch = branch;
			self->branches[branch].unspentOutputs[ref].coinbase = NOT x;
			memcpy(self->branches[branch].unspentOutputs[ref].outputHash,CBTransactionGetHash(block->transactions[x]),32);
			self->branches[branch].unspentOutputs[ref].outputIndex = y;
			self->branches[branch].unspentOutputs[ref].ref.fileID = fileIndex;
			self->branches[branch].unspentOutputs[ref].ref.filePos = cursor;
			// Move cursor past the output
			uint8_t byte = CBByteArrayGetByte(CBGetMessage(block)->bytes, cursor);
			cursor += byte < 253 ? 1 : (byte == 253 ? 2 : (byte == 254 ? 4 : 8));
			cursor += 8;
		}
	}
	// Update validation file.
	if (NOT BEFullValidatorSaveBranchValidator(self, branch)){
		// Failure, truncate block data in case the program closes and the files are inconsistent.
		ftruncate(fileno(fp), size);
		return true; // Still return true as memory is updated.
	}
	// Flush block file
	fflush(fp);
	return true;
}
bool BEFullValidatorAddBlockToOrphans(BEFullValidator * self, CBBlock * block){
	// Save orphan.
	// Add to memory
	self->orphans[self->numOrphans] = block;
	CBRetainObject(block);
	self->numOrphans++;
	// Write new orphan number
	fseek(self->validatorFile, 3, SEEK_SET);
	if (fwrite(&self->numOrphans, 1, 1, self->validatorFile) != 1){
		// Undo
		CBReleaseObject(block);
		self->numOrphans--;
		return false;
	}
	// Write the block
	for (uint8_t x = 0; x < self->numOrphans; x++)
		fseek(self->validatorFile, CBGetMessage(self->orphans[x])->bytes->length, SEEK_CUR);
	if (fwrite(CBByteArrayGetData(CBGetMessage(block)->bytes), 1, CBGetMessage(block)->bytes->length, self->validatorFile) != CBGetMessage(block)->bytes->length){
		// Undo
		CBReleaseObject(block);
		self->numOrphans--;
		fseek(self->validatorFile, 3, SEEK_SET);
		fwrite(&self->numOrphans, 1, 1, self->validatorFile);
		return false;
	}
	// Flush update
	fflush(self->validatorFile);
	return true;
}
BEBlockStatus BEFullValidatorBasicBlockValidation(BEFullValidator * self, CBBlock * block, uint8_t * txHashes, uint64_t networkTime){
	// Get the block hash
	uint8_t * hash = CBBlockGetHash(block);
	// Check if duplicate.
	for (uint8_t x = 0; x < self->numOrphans; x++)
		if (NOT memcmp(CBBlockGetHash(self->orphans[x]), hash, 32))
			return BE_BLOCK_STATUS_DUPLICATE;
	for (uint8_t x = 0; x < self->numBranches; x++){
		bool found;
		BEFullValidatorFindBlockReference(self->branches[x].referenceTable, self->branches[x].numRefs, hash, &found);
		if (found)
			return BE_BLOCK_STATUS_DUPLICATE;
	}
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
	if (res)
		return BE_BLOCK_STATUS_BAD;
	return BE_BLOCK_STATUS_CONTINUE;
}
BEBlockStatus BEFullValidatorBasicBlockValidationCopy(BEFullValidator * self, CBBlock * block, uint8_t * txHashes, uint64_t networkTime){
	uint8_t * hashes = malloc(block->transactionNum * 32);
	if (NOT hashes)
		return BE_BLOCK_STATUS_ERROR;
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
		for (uint32_t y = 1; y < block->transactions[x]->inputNum; y++) {
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
		if (x){
			// Verify values and add to block reward
			if (inputValue < outputValue)
				return BE_BLOCK_VALIDATION_BAD;
			blockReward += inputValue - outputValue;
		}
	}
	// Verify coinbase output for reward
	if (coinbaseOutputValue > blockReward)
		return BE_BLOCK_VALIDATION_BAD;
	return BE_BLOCK_VALIDATION_OK;
}
FILE * BEFullValidatorGetBlockFile(BEFullValidator * self, uint16_t fileID, uint8_t branch){
	// Look to see if the file descriptor is open. Search using linear search because we are almost certainly dealing with a low number of files. Modern filesystems can have filesizes in many terabytes to exabytes.... providing you have the storage obviously.
	FILE * fd;
	for (uint16_t x = 0;; x++) {
		if (x == self->branches[branch].numBlockFiles || self->branches[branch].blockFiles[x].fileID > fileID) {
			// Not in list.
			// Open file.
			char blockFile[strlen(self->dataDir) + 22];
			sprintf(blockFile, "%sblocks%u-%u.dat",self->dataDir, branch, fileID);
			fd = fopen(blockFile, "rb+");
			if (NOT fd && errno == ENOENT)
				// Create file if it doesn't exist
				fd = fopen(blockFile, "wb+");
			if (NOT fd)
				// Cannot open file. The first file was closed. Need to plug the gap...
				return NULL;
			// Expand list
			self->branches[branch].numBlockFiles++;
			self->branches[branch].blockFiles = realloc(self->branches[branch].blockFiles, sizeof(*self->branches[branch].blockFiles) * self->branches[branch].numBlockFiles);
			if (NOT self->branches[branch].blockFiles){
				// Unfortunately going to have to cut a file off. Chop chop chop
				self->branches[branch].numBlockFiles--;
				fclose(self->branches[branch].blockFiles[self->branches[branch].numBlockFiles].file);
			}
			if (x == self->branches[branch].numBlockFiles)
				// We were going to add to the end but it turns out there is not enough memory! :O
				x--;
			if (x != self->branches[branch].numBlockFiles - 1)
				// Move list up unless we are adding to the end.
				memmove(self->branches[branch].blockFiles + x + 1, self->branches[branch].blockFiles + x, sizeof(*self->branches[branch].blockFiles) * (self->branches[branch].numBlockFiles - x - 1));
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
		BEOutputReference * outRef;
		bool found;
		uint32_t i = BEFullValidatorFindOutputReference(self->branches[branch].unspentOutputs, self->branches[branch].numUnspentOutputs,CBByteArrayGetData(allSpentOutputs[transactionIndex][inputIndex].hash),allSpentOutputs[transactionIndex][inputIndex].index,&found);
		if (NOT found)
			// No unspent outputs for this input.
			return BE_BLOCK_VALIDATION_BAD;
		outRef = self->branches[branch].unspentOutputs + i;
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
uint32_t BEFullValidatorFindBlockReference(BEBlockReferenceHashIndex * lookupTable, uint32_t refNum, uint8_t * hash, bool * found){
	// Block branch block reference lists and the unspent output reference list, use sorted lists, therefore this uses an interpolation search which is an optimsation on binary search.
	uint32_t left = 0;
	uint32_t right = refNum - 1;
	uint64_t miniKey = BEHashMiniKey(hash);
	uint64_t leftMiniKey;
	uint64_t rightMiniKey;
	for (;;) {
		BEBlockReferenceHashIndex * leftRef = lookupTable + left;
		BEBlockReferenceHashIndex * rightRef = lookupTable + right;
		if (memcmp(hash, rightRef->blockHash, 32) > 0){
			*found = false;
			return right + 1; // Above the right
		}
		if (memcmp(hash, leftRef->blockHash, 32) < 0) {
			*found = false;
			return left; // Below the left.
		}
		uint32_t pos;
		if (rightMiniKey - leftMiniKey){
			leftMiniKey = BEHashMiniKey(leftRef->blockHash);
			rightMiniKey = BEHashMiniKey(rightRef->blockHash);
			if (rightMiniKey - leftMiniKey)
				pos = left + (uint32_t)( (right - left) * (miniKey - leftMiniKey) ) / (rightMiniKey - leftMiniKey);
			else
				pos = pos = (right + left)/2;
		}else
			pos = pos = (right + left)/2;
		int res = memcmp(hash, lookupTable + pos, 32);
		if (NOT res){
			*found = true;
			return pos;
		}
		else if (res > 0)
			left = pos + 1;
		else
			right = pos - 1;
	}
}
uint32_t BEFullValidatorFindOutputReference(BEOutputReference * refs, uint32_t refNum, uint8_t * hash, uint32_t index, bool * found){
	// Same as BEFullValidatorFindBlockReference but for ouutput references.
	if (NOT refNum) {
		*found = false;
		return 0;
	}
	uint32_t left = 0;
	uint32_t right = refNum - 1;
	uint64_t miniKey = BEHashMiniKey(hash);
	uint64_t leftMiniKey;
	uint64_t rightMiniKey;
	for (;;) {
		BEOutputReference * leftRef = refs + left;
		BEOutputReference * rightRef = refs + right;
		int res = memcmp(hash, rightRef->outputHash, 32);
		if (res > 0 || (NOT res && index > rightRef->outputIndex)) {
			*found = false;
			return right + 1; // Above the right
		}
		res = memcmp(hash, leftRef->outputHash, 32);
		if (res < 0 || (NOT res && index < leftRef->outputIndex)) {
			*found = false;
			return left; // Below the left.
		}
		uint32_t pos;
		if (rightMiniKey - leftMiniKey){
			leftMiniKey = BEHashMiniKey(leftRef->outputHash);
			rightMiniKey = BEHashMiniKey(rightRef->outputHash);
			if (rightMiniKey - leftMiniKey)
				pos = left + (uint32_t)( (right - left) * (miniKey - leftMiniKey) ) / (rightMiniKey - leftMiniKey);
			else
				pos = pos = (right + left)/2;
		}else
			pos = pos = (right + left)/2;
		res = memcmp(hash, (refs + pos)->outputHash, 32);
		if (NOT res && index == (refs + pos)->outputIndex){
			*found = true;
			return pos;
		}
		else if (res > 0 || (NOT res && index > (refs + pos)->outputIndex))
			left = pos + 1;
		else
			right = pos - 1;
	}
}
CBBlock * BEFullValidatorLoadBlock(BEFullValidator * self, BEBlockReference blockRef, uint32_t branch){
	// Get the file
	FILE * fd = BEFullValidatorGetBlockFile(self, blockRef.ref.fileID, branch);
	if (NOT fd)
		return NULL;
	// Now we have the file and it is open we need the block. Get the length of the block.
	uint8_t length[4];
	fseek(fd, blockRef.ref.filePos, SEEK_SET);
	if (fread(length, 1, 4, fd) != 4)
		return NULL;
	CBByteArray * data = CBNewByteArrayOfSize(length[3] << 24 | length[2] << 16 | length[1] << 8 | length[0], self->onErrorReceived);
	// Now read block data
	if (fread(CBByteArrayGetData(data), 1, data->length, fd) != data->length) {
		CBReleaseObject(data);
		return NULL;
	}
	// Make and return the block
	CBBlock * block = CBNewBlockFromData(data, self->onErrorReceived);
	CBReleaseObject(data);
	return block;
}
bool BEFullValidatorLoadBranchValidator(BEFullValidator * self, uint8_t branch){
	if (self->numBranches && self->numBranches <= BE_MAX_BRANCH_CACHE) {
		// Open branch data file
		unsigned long dataDirLen = strlen(self->dataDir);
		char * branchFilePath = malloc(dataDirLen + strlen(BE_ADDRESS_DATA_FILE) + 1);
		sprintf(branchFilePath, "%sbranch%u.dat",self->dataDir, branch);
		self->branches[branch].branchValidationFile = fopen(branchFilePath, "rb+");
		if (self->branches[branch].branchValidationFile) {
			// The validator file exists.
			free(branchFilePath);
			// Get the file length
			fseek(self->branches[branch].branchValidationFile, 0, SEEK_END);
			unsigned long fileLen = ftell(self->branches[branch].branchValidationFile);
			fseek(self->branches[branch].branchValidationFile, 0, SEEK_SET);
			// Copy file contents into buffer.
			CBByteArray * buffer = CBNewByteArrayOfSize((uint32_t)fileLen, self->onErrorReceived);
			if (NOT buffer) {
				fclose(self->branches[branch].branchValidationFile);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create buffer of size %u.",fileLen);
				return false;
			}
			size_t res = fread(CBByteArrayGetData(buffer), 1, fileLen, self->branches[branch].branchValidationFile);
			if(res != fileLen){
				CBReleaseObject(buffer);
				fclose(self->validatorFile);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not read %u bytes of data into buffer. fread returned %u",fileLen,res);
				return false;
			}
			// Deserailise data
			if (buffer->length >= 26){
				self->branches[branch].numRefs = CBByteArrayReadInt32(buffer, 0);
				if (buffer->length >= 26 + self->branches[branch].numRefs*54) {
					self->branches[branch].references = malloc(sizeof(*self->branches[branch].references) * self->branches[branch].numRefs);
					if (self->branches[branch].references) {
						self->branches[branch].referenceTable = malloc(sizeof(*self->branches[branch].referenceTable) * self->branches[branch].numRefs);
						if (self->branches[branch].referenceTable) {
							uint32_t cursor = 4;
							for (uint32_t x = 0; x < self->branches[branch].numRefs; x++) {
								// Load block reference
								self->branches[branch].references[x].ref.fileID = CBByteArrayReadInt16(buffer, cursor);
								cursor += 2;
								self->branches[branch].references[x].ref.filePos = CBByteArrayReadInt64(buffer, cursor);
								cursor += 8;
								self->branches[branch].references[x].target = CBByteArrayReadInt32(buffer, cursor);
								cursor += 4;
								self->branches[branch].references[x].time = CBByteArrayReadInt32(buffer, cursor);
								cursor += 4;
								// Load block reference index
								memcpy(self->branches[branch].referenceTable[x].blockHash, CBByteArrayGetData(buffer) + cursor, 32);
								cursor += 32;
								self->branches[branch].referenceTable[x].index = CBByteArrayReadInt32(buffer, cursor);
								cursor += 4;
							}
							self->branches[branch].lastRetargetTime = CBByteArrayReadInt32(buffer, cursor);
							cursor += 4;
							self->branches[branch].parentBranch = CBByteArrayGetByte(buffer, cursor);
							cursor++;
							self->branches[branch].parentBlockIndex = CBByteArrayReadInt32(buffer, cursor);
							cursor+= 4;
							self->branches[branch].startHeight = CBByteArrayReadInt32(buffer, cursor);
							cursor+= 4;
							self->branches[branch].lastValidation = CBByteArrayReadInt32(buffer, cursor);
							cursor+= 4;
							self->branches[branch].numUnspentOutputs = CBByteArrayReadInt32(buffer, cursor);
							cursor += 4;
							if (buffer->length >= cursor + self->branches[branch].numUnspentOutputs*52 + 1) {
								self->branches[branch].unspentOutputs = malloc(sizeof(*self->branches[branch].unspentOutputs) * self->branches[branch].numUnspentOutputs);
								if (self->branches[branch].unspentOutputs) {
									for (uint32_t x = 0; x < self->branches[branch].numUnspentOutputs; x++) {
										memcpy(self->branches[branch].unspentOutputs[x].outputHash, CBByteArrayGetData(buffer) + cursor, 32);
										cursor += 32;
										self->branches[branch].unspentOutputs[x].outputIndex = CBByteArrayReadInt32(buffer, cursor);
										cursor += 4;
										self->branches[branch].unspentOutputs[x].ref.fileID = CBByteArrayReadInt16(buffer, cursor);
										cursor += 2;
										self->branches[branch].unspentOutputs[x].ref.filePos = CBByteArrayReadInt64(buffer, cursor);
										cursor += 8;
										self->branches[branch].unspentOutputs[x].height = CBByteArrayReadInt32(buffer, cursor);
										cursor += 4;
										self->branches[branch].unspentOutputs[x].coinbase = CBByteArrayGetByte(buffer, cursor);
										cursor += 1;
										self->branches[branch].unspentOutputs[x].branch = CBByteArrayGetByte(buffer, cursor);
										cursor += 1;
									}
									// Get work
									self->branches[branch].work.length = CBByteArrayGetByte(buffer, cursor);
									cursor++;
									if (buffer->length >= cursor + self->branches[branch].work.length) {
										// Allocate bigint
										if (CBBigIntAlloc(&self->branches[branch].work, self->branches[branch].work.length)) {
											memcpy(self->branches[branch].work.data, CBByteArrayGetData(buffer) + cursor,self->branches[branch].work.length);
											return true;
										}else
											self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"CBBigIntAloc failed in BEFullValidatorLoadBranchValidator for %u bytes", self->branches[branch].work.length);
									}else
										self->onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the branch work %u < %u",buffer->length, cursor + self->branches[branch].work.length);
								}else
									self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for unspent outputs in BEFullValidatorLoadBranchValidator for branch",sizeof(*self->branches[branch].unspentOutputs) * self->branches[branch].numUnspentOutputs);
							}else
								self->onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"There is not enough data for the unspent outputs. %i < %i",buffer->length, cursor + self->branches[branch].numUnspentOutputs*52 + 1);
						}else
							self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for the reference table in BEFullValidatorLoadBranchValidator.",sizeof(*self->branches[branch].referenceTable) * self->branches[branch].numRefs);
						free(self->branches[branch].references);
					}else
						self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for references in BEFullValidatorLoadBranchValidator.",sizeof(*self->branches[branch].references) * self->branches[branch].numRefs);
				}else
					self->onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the references %u < %u",buffer->length, 26 + self->branches[branch].numRefs*54);
			}else
				self->onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the number of references %u < 26",buffer->length);
			CBReleaseObject(buffer);
			return false;
		}else if (NOT branch){
			// The branch file does not exist. Create the file.
			self->branches[branch].branchValidationFile = fopen(branchFilePath, "wb+");
			free(branchFilePath);
			if (NOT self->branches[branch].branchValidationFile) {
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not open the branch validator file BEFullValidatorLoadBranchValidator.");
				return false;
			}
			// Allocate data
			self->branches[0].references = malloc(sizeof(*self->branches[0].references));
			if (NOT self->branches[0].references) {
				self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the genesis reference in BEFullValidatorLoadBranchValidator.",sizeof(*self->branches[0].references));
				return false;
			}
			self->branches[0].referenceTable = malloc(sizeof(*self->branches[0].referenceTable));
			if (NOT self->branches[0].referenceTable) {
				self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for the reference lookup table in BEFullValidatorLoadBranchValidator.",sizeof(*self->branches[0].referenceTable));
				free(self->branches[0].references);
				return false;
			}
			// Allocate unspent outputs
			self->branches[0].numUnspentOutputs = 1;
			self->branches[0].unspentOutputs = malloc(sizeof(*self->branches[0].unspentOutputs));
			if (NOT self->branches[0].unspentOutputs) {
				self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u bytes of memory for the unspent outputs in BEFullValidatorLoadBranchValidator.",sizeof(*self->branches[0].unspentOutputs));
				free(self->branches[0].references);
				free(self->branches[0].referenceTable);
				return false;
			}
			// Initialise data with the genesis block.
			self->branches[0].lastRetargetTime = 1231006505;
			self->branches[0].startHeight = 0;
			self->branches[0].numRefs = 1;
			self->branches[0].lastValidation = 0;
			uint8_t genesisHash[32] = {0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00};
			self->branches[0].references[0].ref.fileID = 0;
			self->branches[0].references[0].ref.filePos = 0;
			self->branches[0].references[0].target = CB_MAX_TARGET;
			self->branches[0].references[0].time = 1231006505;
			self->branches[0].work.length = 1;
			self->branches[0].work.data = malloc(1);
			if (NOT self->branches[0].work.data) {
				self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate 1 byte of memory for the initial branch work in BEFullValidatorLoadBranchValidator.");
				free(self->branches[0].references);
				free(self->branches[0].referenceTable);
				free(self->branches[0].unspentOutputs);
				return false;
			}
			self->branches[0].work.data[0] = 0;
			memcpy(self->branches[0].referenceTable[0].blockHash,genesisHash,32);
			self->branches[0].referenceTable[0].index = 0;
			// The output in the genesis block
			self->branches[0].unspentOutputs[0].branch = 0;
			self->branches[0].unspentOutputs[0].coinbase = true;
			self->branches[0].unspentOutputs[0].height = 0;
			uint8_t genesisCoinbaseHash[32] = {0x3b,0xa3,0xed,0xfd,0x7a,0x7b,0x12,0xb2,0x7a,0xc7,0x2c,0x3e,0x67,0x76,0x8f,0x61,0x7f,0xc8,0x1b,0xc3,0x88,0x8a,0x51,0x32,0x3a,0x9f,0xb8,0xaa,0x4b,0x1e,0x5e,0x4a};
			memcpy(self->branches[0].unspentOutputs[0].outputHash,genesisCoinbaseHash,32);
			self->branches[0].unspentOutputs[0].ref.fileID = 0;
			self->branches[0].unspentOutputs[0].ref.filePos = 209;
			// Write genesis block to the first block file
			char * blockFilePath = malloc(dataDirLen + 14);
			if (NOT blockFilePath) {
				self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the first block file path in BEFullValidatorLoadBranchValidator.",dataDirLen + 12);
				free(self->branches[0].references);
				free(self->branches[0].referenceTable);
				free(self->branches[0].unspentOutputs);
				return false;
			}
			memcpy(blockFilePath, self->dataDir, dataDirLen);
			strcpy(blockFilePath + dataDirLen, "blocks0-0.dat");
			self->branches[0].blockFiles = malloc(sizeof(*self->branches[0].blockFiles));
			if (NOT self->branches[0].blockFiles) {
				free(blockFilePath);
				self->onErrorReceived(CB_ERROR_OUT_OF_MEMORY,"Could not allocate %u byte of memory for the blockFiles list in BEFullValidatorLoadBranchValidator.",sizeof(*self->branches[0].blockFiles));
				free(self->branches[0].references);
				free(self->branches[0].referenceTable);
				free(self->branches[0].unspentOutputs);
				return false;
			}
			self->branches[0].blockFiles[0].fileID = 0;
			self->branches[0].blockFiles[0].file = BEFullValidatorGetBlockFile(self, 0, 0);
			if (NOT self->branches[0].blockFiles[0].file) {
				free(self->branches[0].blockFiles);
				free(blockFilePath);
				free(self->branches[0].references);
				free(self->branches[0].referenceTable);
				free(self->branches[0].unspentOutputs);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not open the first block file in BEFullValidatorLoadBranchValidator.");
				return false;
			}
			// Write genesis block data. Begins with length.
			if(fwrite((uint8_t []){
				0x1D,0x01,0x00,0x00, // The Length of the block is 285 bytes or 0x11D
				0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
				0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3B,0xA3,0xED,0xFD,0x7A,0x7B,0x12,0xB2,0x7A,0xC7,0x2C,0x3E,0x67,0x76,
				0x8F,0x61,0x7F,0xC8,0x1B,0xC3,0x88,0x8A,0x51,0x32,0x3A,0x9F,0xB8,0xAA,0x4B,0x1E,0x5E,0x4A,0x29,0xAB,0x5F,0x49,0xFF,0xFF,0x00,
				0x1D,0x1D,0xAC,0x2B,0x7C,0x01,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
				0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x4D,0x04,0xFF,
				0xFF,0x00,0x1D,0x01,0x04,0x45,0x54,0x68,0x65,0x20,0x54,0x69,0x6D,0x65,0x73,0x20,0x30,0x33,0x2F,0x4A,0x61,0x6E,0x2F,0x32,0x30,
				0x30,0x39,0x20,0x43,0x68,0x61,0x6E,0x63,0x65,0x6C,0x6C,0x6F,0x72,0x20,0x6F,0x6E,0x20,0x62,0x72,0x69,0x6E,0x6B,0x20,0x6F,0x66,
				0x20,0x73,0x65,0x63,0x6F,0x6E,0x64,0x20,0x62,0x61,0x69,0x6C,0x6F,0x75,0x74,0x20,0x66,0x6F,0x72,0x20,0x62,0x61,0x6E,0x6B,0x73,
				0xFF,0xFF,0xFF,0xFF,0x01,0x00,0xF2,0x05,0x2A,0x01,0x00,0x00,0x00,0x43,0x41,0x04,0x67,0x8A,0xFD,0xB0,0xFE,0x55,0x48,0x27,0x19,
				0x67,0xF1,0xA6,0x71,0x30,0xB7,0x10,0x5C,0xD6,0xA8,0x28,0xE0,0x39,0x09,0xA6,0x79,0x62,0xE0,0xEA,0x1F,0x61,0xDE,0xB6,0x49,0xF6,
				0xBC,0x3F,0x4C,0xEF,0x38,0xC4,0xF3,0x55,0x04,0xE5,0x1E,0xC1,0x12,0xDE,0x5C,0x38,0x4D,0xF7,0xBA,0x0B,0x8D,0x57,0x8A,0x4C,0x70,
				0x2B,0x6B,0xF1,0x1D,0x5F,0xAC,0x00,0x00,0x00,0x00}, 1, 289, self->branches[0].blockFiles[0].file) != 289){
				fclose(self->branches[0].blockFiles[0].file);
				free(self->branches[0].blockFiles);
				free(blockFilePath);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not write the genesis block in BEFullValidatorLoadBranchValidator.");
				free(self->branches[0].references);
				free(self->branches[0].referenceTable);
				free(self->branches[0].unspentOutputs);
				return false;
			}
			// Write to the branch file
			if(NOT BEFullValidatorSaveBranchValidator(self, branch)){
				fclose(self->branches[0].blockFiles[0].file);
				free(self->branches[0].blockFiles);
				free(blockFilePath);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not write the validation data in BEFullValidatorLoadBranchValidator.");
				free(self->branches[0].references);
				free(self->branches[0].referenceTable);
				free(self->branches[0].unspentOutputs);
				return false;
			}
			// Flush data
			fflush(self->branches[0].blockFiles[0].file);
			free(blockFilePath);
			return true;
		}
	}
	return false;
}
bool BEFullValidatorLoadValidator(BEFullValidator * self){
	if (NOT self->validatorFile) {
		unsigned long dataDirLen = strlen(self->dataDir);
		char * validatorFilePath = malloc(dataDirLen + strlen(BE_VALIDATION_DATA_FILE) + 1);
		memcpy(validatorFilePath, self->dataDir, dataDirLen);
		strcpy(validatorFilePath + dataDirLen, BE_VALIDATION_DATA_FILE);
		self->validatorFile = fopen(validatorFilePath, "rb+");
		if (self->validatorFile) {
			// Validation data exists
			free(validatorFilePath);
			// Get the file length
			fseek(self->validatorFile, 0, SEEK_END);
			unsigned long fileLen = ftell(self->validatorFile);
			fseek(self->validatorFile, 0, SEEK_SET);
			// Copy file contents into buffer.
			CBByteArray * buffer = CBNewByteArrayOfSize((uint32_t)fileLen, self->onErrorReceived);
			if (NOT buffer) {
				fclose(self->validatorFile);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create buffer of size %u.",fileLen);
				return false;
			}
			size_t res = fread(CBByteArrayGetData(buffer), 1, fileLen, self->validatorFile);
			if(res != fileLen){
				CBReleaseObject(buffer);
				fclose(self->validatorFile);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not read %u bytes of data into buffer. fread returned %u",fileLen,res);
				return false;
			}
			// Deserailise data
			if (buffer->length >= 3){
				self->mainBranch = CBByteArrayGetByte(buffer, 0);
				self->numBranches = CBByteArrayGetByte(buffer, 1);
				// Now do orhpans
				self->numOrphans = CBByteArrayGetByte(buffer, 2);
				uint8_t cursor = 3;
				if (buffer->length >= cursor + self->numOrphans*82){
					for (uint8_t x = 0; x < self->numOrphans; x++) {
						bool err = false;
						CBByteArray * data = CBNewByteArraySubReference(buffer, cursor, buffer->length - 56);
						if (data) {
							self->orphans[x] = CBNewBlockFromData(data, self->onErrorReceived);
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
										self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create byte copy for orphan %u",x);
										err = true;
									}
								}else{
									self->onErrorReceived(CB_ERROR_MESSAGE_SERIALISATION_BAD_BYTES,"Could not deserailise orphan %u",x);
									err = true;
								}
								if (err)
									CBReleaseObject(self->orphans[x]);
							}else{
								self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create block object for orphan %u",x);
								err = true;
							}
							if (err)
								CBReleaseObject(data);
						}else{
							self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create byte reference for orphan %u",x);
							err = true;
						}
						if (err){
							for (uint8_t y = 0; y < x; y++)
								CBReleaseObject(self->orphans[y]);
							CBReleaseObject(buffer);
							fclose(self->validatorFile);
							return false;
						}
					}
					// Success
					CBReleaseObject(buffer);
					// All done
					return true;
				}else
					self->onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"There is not enough data for the orhpans. %i < %i",buffer->length, cursor + self->numOrphans*82);
			}else
				self->onErrorReceived(CB_ERROR_MESSAGE_DESERIALISATION_BAD_BYTES,"Not enough data for the minimum required data %u < 3",buffer->length);
			CBReleaseObject(buffer);
			fclose(self->validatorFile);
			return false;
		}else{
			// Create initial data
			// Create directory if it doesn't already exist
			if(mkdir(self->dataDir, S_IREAD | S_IWRITE))
				if (errno != EEXIST){
					free(validatorFilePath);
					self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not create the data directory.");
					return false;
				}
			// Open validator file
			self->validatorFile = fopen(validatorFilePath, "wb");
			free(validatorFilePath);
			if (NOT self->validatorFile){
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not open the validator file.");
				return false;
			}
			self->numOrphans = 0;
			self->numBranches = 1;
			self->mainBranch = 0;
			// Write initial validator data
			if(NOT BEFullValidatorSaveValidator(self)){
				fclose(self->validatorFile);
				self->onErrorReceived(CB_ERROR_INIT_FAIL,"Could not write initial data to the valdiator file.");
				return false;
			}
			return true;
		}
	}
	return false;
}
BEBlockStatus BEFullValidatorProcessBlock(BEFullValidator * self, CBBlock * block, uint64_t networkTime){
	bool found;
	// Get transaction hashes.
	uint8_t * txHashes = malloc(32 * block->transactionNum);
	if (NOT txHashes)
		return BE_BLOCK_STATUS_ERROR;
	// Put the hashes for transactions into a list.
	for (uint32_t x = 0; x < block->transactionNum; x++)
		memcpy(txHashes + 32*x, CBTransactionGetHash(block->transactions[x]), 32);
	// Determine what type of block this is.
	uint8_t prevBranch = 0;
	uint32_t prevBlockIndex;
	for (; prevBranch < self->numBranches; prevBranch++){
		prevBlockIndex = self->branches[prevBranch].referenceTable[BEFullValidatorFindBlockReference(self->branches[prevBranch].referenceTable, self->branches[prevBranch].numRefs, CBByteArrayGetData(block->prevBlockHash),&found)].index;
		if (found)
			// The block is extending this branch or creating a side branch to this branch
			break;
	}
	if (prevBranch == self->numBranches){
		// Orphan block. End here.
		if (self->numOrphans == BE_MAX_ORPHAN_CACHE){
			free(txHashes);
			return BE_BLOCK_STATUS_MAX_CACHE;
		}
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
	uint8_t branch;
	uint32_t nextHeight = self->branches[prevBranch].startHeight + prevBlockIndex + 1;
	if (prevBlockIndex == self->branches[prevBranch].numRefs - 1) {
		// Extension
		// Do basic validation with a copy of the transaction hashes.
		BEBlockStatus res = BEFullValidatorBasicBlockValidationCopy(self, block, txHashes, networkTime);
		if (res != BE_BLOCK_STATUS_CONTINUE){
			free(txHashes);
			return res;
		}
		branch = prevBranch;
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
		branch = self->numBranches;
		// Initialise minimal data the new branch.
		// Record parent branch.
		self->branches[branch].parentBranch = prevBranch;
		self->branches[branch].parentBlockIndex = prevBlockIndex;
		// Set retarget time
		self->branches[branch].lastRetargetTime = self->branches[prevBranch].lastRetargetTime;
		// Calculate the work
		self->branches[branch].work.length = self->branches[prevBranch].work.length;
		self->branches[branch].work.data = malloc(self->branches[branch].work.length);
		// Copy work over
		memcpy(self->branches[branch].work.data, self->branches[prevBranch].work.data, self->branches[branch].work.length);
		// Remove later block work down to the fork
		for (uint32_t y = nextHeight - self->branches[prevBranch].startHeight; y < self->branches[prevBranch].numRefs; y++) {
			CBBigInt tempWork;
			if (NOT CBCalculateBlockWork(&tempWork,self->branches[prevBranch].references[y].target)){
				free(txHashes);
				return BE_BLOCK_STATUS_ERROR;
			}
			CBBigIntEqualsSubtractionByBigInt(&self->branches[branch].work, &tempWork);
			free(tempWork.data); 
		}
		self->branches[branch].lastValidation = BE_NO_VALIDATION;
	}
	// Check timestamp six blocks back
	uint8_t tempBranch = prevBranch;
	uint32_t tempBlockIndex = prevBlockIndex;
	uint8_t x = 6;
	for (;;) {
		if (tempBlockIndex >= x) {
			tempBlockIndex -= x;
			break;
		}
		if (NOT tempBranch){
			// Cannot go back further.
			tempBlockIndex = 0;
			break;
		}
		tempBranch = self->branches[tempBranch].parentBranch;
		tempBlockIndex = self->branches[tempBranch].numRefs - 1;
		x -= tempBlockIndex;
	}
	// Check timestamp
	if (block->time < self->branches[tempBranch].references[tempBlockIndex].time){
		free(txHashes);
		return BE_BLOCK_STATUS_BAD;
	}
	uint32_t target;
	bool change = NOT (nextHeight % 2016);
	if (change)
		// Difficulty change for this branch
		target = CBCalculateTarget(self->branches[prevBranch].references[prevBlockIndex].target, block->time - self->branches[branch].lastRetargetTime);
	else
		target = self->branches[prevBranch].references[prevBlockIndex].target;
	// Check target
	if (block->target != target){
		free(txHashes);
		return BE_BLOCK_STATUS_BAD;
	}
	// Calculate total work
	CBBigInt work;
	if (NOT CBCalculateBlockWork(&work, block->target)){
		free(txHashes);
		return BE_BLOCK_STATUS_ERROR;
	}
	if (NOT CBBigIntEqualsAdditionByBigInt(&work, &self->branches[branch].work)){
		free(txHashes);
		return BE_BLOCK_STATUS_ERROR;
	}
	if (branch != self->mainBranch) {
		// Check if the block is adding to a side branch without becoming the main branch
		if (CBBigIntCompareToBigInt(&work,&self->branches[self->mainBranch].work) != CB_COMPARE_MORE_THAN){
			free(txHashes);
			// Add to branch without complete validation
			if (NOT BEFullValidatorAddBlockToBranch(self, branch, block))
				// Failure in adding block.
				return BE_BLOCK_STATUS_ERROR;
			return BE_BLOCK_STATUS_SIDE;
		}
		// Potential block-chain reorganisation. Validate the side branch. THIS NEEDS TO START AT THE FIRST BRANCH BACK WHERE VALIDATION IS NOT COMPLETE AND GO UP THROUGH THE BLOCKS VALIDATING THEM. Includes Prior Branches!
		tempBranch = prevBranch;
		uint32_t lastBlocks[3]; // Used to store the last blocks in each branch going to the branch we are validating for.
		uint8_t lastBlocksIndex = 3; // The starting point of lastBlocks. If 3 then we are only validating the branch we added to.
		uint8_t branches[3]; // Branches to go to after the last blocks.
		// Go back until we find the branch with validated blocks in it.
		while (self->branches[self->branches[tempBranch].parentBranch].lastValidation == BE_NO_VALIDATION){
			branches[--lastBlocksIndex] = tempBranch;
			tempBranch = self->branches[tempBranch].parentBranch;
			lastBlocks[lastBlocksIndex] = self->branches[tempBranch].parentBlockIndex;
		}
		tempBlockIndex = self->branches[self->branches[tempBranch].parentBranch].lastValidation;
		// Now validate all blocks going up.
		uint8_t * txHashes2 = NULL;
		uint32_t txHashes2AllocSize = 0;
		while (tempBlockIndex != self->branches[branch].numRefs - 1 || tempBranch != branch) {
			// Get block
			CBBlock * tempBlock = BEFullValidatorLoadBlock(self, self->branches[tempBranch].references[tempBlockIndex],tempBranch);
			if (NOT tempBlock) {
				free(txHashes);
				return BE_BLOCK_STATUS_ERROR;
			}
			// Get transaction hashes
			if (txHashes2AllocSize < tempBlock->transactionNum * 32) {
				txHashes2AllocSize = tempBlock->transactionNum * 32;
				txHashes2 = realloc(txHashes2, txHashes2AllocSize);
				if (NOT txHashes2){
					free(txHashes);
					return BE_BLOCK_STATUS_ERROR;
				}
			}
			for (uint32_t x = 0; x < tempBlock->transactionNum; x++)
				memcpy(txHashes2 + 32*x, CBTransactionGetHash(tempBlock->transactions[x]), 32);
			// Validate block
			BEBlockValidationResult res = BEFullValidatorCompleteBlockValidation(self, tempBranch, tempBlock, txHashes2, self->branches[tempBranch].startHeight + tempBlockIndex);
			if (res == BE_BLOCK_VALIDATION_BAD){
				free(txHashes);
				return BE_BLOCK_STATUS_BAD;
			}
			if (res == BE_BLOCK_VALIDATION_ERR){
				free(txHashes);
				return BE_BLOCK_STATUS_ERROR;
			}
			// This block passed validation. Move onto next block.
			if (tempBlockIndex == lastBlocks[lastBlocksIndex]) {
				// Came to the last block in the branch
				tempBranch = branches[lastBlocksIndex];
				tempBlockIndex = 0;
				lastBlocksIndex++;
			}else
				tempBlockIndex++;
			CBReleaseObject(tempBlock);
		}
		free(txHashes2);
		// Now we validate the block for the new main chain.
	}
	// We are just validating a new block on the main chain
	BEBlockValidationResult res = BEFullValidatorCompleteBlockValidation(self, branch, block, txHashes, self->branches[branch].startHeight + self->branches[branch].numRefs);
	free(txHashes);
	switch (res) {
		case BE_BLOCK_VALIDATION_BAD:
			return BE_BLOCK_STATUS_BAD;
		case BE_BLOCK_VALIDATION_ERR:
			return BE_BLOCK_STATUS_ERROR;
		case BE_BLOCK_VALIDATION_OK:
			// Update branch and unspent outputs.
			if (NOT BEFullValidatorAddBlockToBranch(self, branch, block))
				// Failure in adding block.
				return BE_BLOCK_STATUS_ERROR;
			return BE_BLOCK_STATUS_MAIN;
	}
}
bool BEFullValidatorSaveBranchValidator(BEFullValidator * self, uint8_t branch){
	// Serailise into byte array and then write the byte array to the file.
	CBByteArray * data = CBNewByteArrayOfSize(self->branches[branch].numRefs*54 + self->branches[branch].numUnspentOutputs*52 + 26 + self->branches[branch].work.length, self->onErrorReceived);
	if (NOT data)
		return false;
	CBByteArraySetInt32(data, 0, self->branches[branch].numRefs);
	uint32_t cursor = 4;
	for (uint32_t x = 0; x < self->branches[branch].numRefs; x++) {
		// Data for block reference
		CBByteArraySetInt16(data, cursor, self->branches[branch].references[x].ref.fileID);
		cursor += 2;
		CBByteArraySetInt64(data, cursor, self->branches[branch].references[x].ref.filePos);
		cursor += 8;
		CBByteArraySetInt32(data, cursor, self->branches[branch].references[x].target);
		cursor += 4;
		CBByteArraySetInt32(data, cursor, self->branches[branch].references[x].time);
		cursor += 4;
		// Data for block reference index
		CBByteArraySetBytes(data, cursor, self->branches[branch].referenceTable[x].blockHash, 32);
		cursor += 32;
		CBByteArraySetInt32(data, cursor, self->branches[branch].referenceTable[x].index);
		cursor += 4;
	}
	CBByteArraySetInt32(data, cursor, self->branches[branch].lastRetargetTime);
	cursor += 4;
	CBByteArraySetByte(data, cursor, self->branches[branch].parentBranch);
	cursor++;
	CBByteArraySetInt32(data, cursor, self->branches[branch].parentBlockIndex);
	cursor+= 4;
	CBByteArraySetInt32(data, cursor, self->branches[branch].startHeight);
	cursor+= 4;
	CBByteArraySetInt32(data, cursor, self->branches[branch].lastValidation);
	cursor+= 4;
	CBByteArraySetInt32(data, cursor, self->branches[branch].numUnspentOutputs);
	cursor += 4;
	for (uint32_t x = 0; x < self->branches[branch].numUnspentOutputs; x++) {
		CBByteArraySetBytes(data, cursor, self->branches[branch].unspentOutputs[x].outputHash, 32);
		cursor += 32;
		CBByteArraySetInt32(data, cursor, self->branches[branch].unspentOutputs[x].outputIndex);
		cursor += 4;
		CBByteArraySetInt16(data, cursor, self->branches[branch].unspentOutputs[x].ref.fileID);
		cursor += 2;
		CBByteArraySetInt64(data, cursor, self->branches[branch].unspentOutputs[x].ref.filePos);
		cursor += 8;
		CBByteArraySetInt32(data, cursor, self->branches[branch].unspentOutputs[x].height);
		cursor += 4;
		CBByteArraySetByte(data, cursor, self->branches[branch].unspentOutputs[x].coinbase);
		cursor += 1;
		CBByteArraySetInt32(data, cursor, self->branches[branch].unspentOutputs[x].branch);
		cursor += 1;
	}
	CBByteArraySetByte(data, cursor, self->branches[branch].work.length);
	cursor++;
	CBByteArraySetBytes(data, cursor, self->branches[branch].work.data, self->branches[branch].work.length);
	// Write data to file.
	fseek(self->branches[0].branchValidationFile, 0, SEEK_SET);
	size_t res = fwrite(CBByteArrayGetData(data), 1, data->length, self->branches[0].branchValidationFile);
	CBReleaseObject(data);
	if (res != cursor + self->branches[branch].work.length)
		return false;
	// Flush data
	fflush(self->branches[0].branchValidationFile);
	return true;
}
bool BEFullValidatorSaveValidator(BEFullValidator * self){
	fseek(self->validatorFile, 0, SEEK_SET);
	if (fwrite((uint8_t []){self->mainBranch,self->numBranches,self->numOrphans}, 1, 3, self->validatorFile) != 3)
		return false;
	fflush(self->validatorFile);
	return true;
}
