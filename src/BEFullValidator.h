//
//  BEFullValidator.h
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

/**
 @file
 @brief Validates blocks, finding the main chain.
 */

#ifndef BEFULLVALIDATORH
#define BEFULLVALIDATORH

#include "BEConstants.h"
#include "CBBlock.h"
#include "CBBigInt.h"
#include "CBValidationFunctions.h"
#include "stdio.h"
#include "string.h"
#include <sys/resource.h>
#include <sys/syslimits.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

/**
 @brief References a part of block storage.
 */
typedef struct{
	uint16_t fileID; /**< The file being referenced. */
	uint64_t filePos; /**< The position in the file which is being referenced. */
} BEFileReference;

/**
 @brief References an output in the block storage.
 */
typedef struct{
	uint8_t outputHash[32]; /** The transaction hash for the output */
	BEFileReference ref; /**< The file reference for the output */
	uint32_t outputIndex; /** The index for the output */
	uint32_t height; /**< Block height of the output */
	bool coinbase; /**< True if a coinbase output */
	uint8_t branch; /**< The branch this output belongs to. */
}BEOutputReference;

/**
 @brief References a block in the block storage.
 */
typedef struct{
	BEFileReference ref; /**< The file reference for the block */
	uint32_t target; /** The target for this block */
	uint32_t time; /**< The block's timestamp */
}BEBlockReference;

/**
 @brief Indexes the block references to the block hashes. When stored as a sorted lookup table, an interpolation search can be used to find the index at which a block reference is stored.
 */
typedef struct{
	uint8_t blockHash[32]; /**< The block hash. */
	uint32_t index; /**< The index for the block reference for this hash */
} BEBlockReferenceHashIndex;

/**
 @brief A FILE object for block storage and the index of this file.
 */
typedef struct{
	FILE * file;
	uint16_t fileID;
}BEBlockFile;

/**
 @brief Represents a block branch.
 */
typedef struct{
	uint32_t numRefs; /**< The number of block references in the branch */
	BEBlockReference * references; /**< The block references */
	BEBlockReferenceHashIndex * referenceTable; /**< The lookup table for block references */
	uint32_t lastRetargetTime; /**< The block timestamp at the last retarget. */
	uint8_t parentBranch; /**< The branch this branch is connected to. */
	uint32_t parentBlockIndex; /**< The block index in the parent branch which this branch is connected to */
	uint32_t startHeight; /**< The starting height where this branch begins */
	uint32_t lastValidation; /**< The index of the last block in this branch that has been fully validated. */
	uint32_t numUnspentOutputs; /**< The number of unspent outputs for this branch upto the last validated block. */
	BEOutputReference * unspentOutputs; /**< A list of unspent outputs for this branch upto the last validated block. */
	CBBigInt work; /**< The total work for this branch. The branch with the highest work is the winner! */
	BEBlockFile * blockFiles; /**< Open block files for this branch. */
	uint16_t numBlockFiles; /**< Number of open block files for this branch. */
	FILE * branchValidationFile; /** The file for the branch validation data, NULL if not open */
} BEBlockBranch;

/**
 @brief Structure for BEFullValidator objects. @see BEFullValidator.h
 */
typedef struct{
	CBObject base;
	FILE * validatorFile; /**< The file for the validation data */
	uint8_t numOrphans; /**< The number of orhpans */
	CBBlock * orphans[BE_MAX_ORPHAN_CACHE]; /**< The ophan block references */
	uint8_t mainBranch; /**< The index for the main branch */
	uint8_t numBranches; /**< The number of block-chain branches. Cannot exceed BE_MAX_BRANCH_CACHE */
	BEBlockBranch branches[BE_MAX_BRANCH_CACHE]; /**< The block-chain branches. */
	char * dataDir; /**< Data directory path */
	void (*onErrorReceived)(CBError error,char *,...); /**< Pointer to error callback */
	uint64_t fileSizeLimit; /**< The maximum allowed filesize */
} BEFullValidator;

/**
 @brief Creates a new BEFullValidator object.
 @returns A new BEFullValidator object.
 */

BEFullValidator * BENewFullValidator(char * dataDir, void (*onErrorReceived)(CBError error,char *,...));

/**
 @brief Gets a BEFullValidator from another object. Use this to avoid casts.
 @param self The object to obtain the BEFullValidator from.
 @returns The BEFullValidator object.
 */
BEFullValidator * BEGetFullValidator(void * self);

/**
 @brief Initialises a BEFullValidator object.
 @param self The BEFullValidator object to initialise.
 @returns true on success, false on failure.
 */
bool BEInitFullValidator(BEFullValidator * self, char * homeDir, void (*onErrorReceived)(CBError error,char *,...));

/**
 @brief Frees a BEFullValidator object.
 @param self The BEFullValidator object to free.
 */
void BEFreeFullValidator(void * self);

// Functions

/**
 @brief Adds a block to a branch.
 @param self The BEFullValidator object.
 @param branch The index of the branch to add the block to.
 @param block The block to add.
 @returns true on success and false on error.
 */
bool BEFullValidatorAddBlockToBranch(BEFullValidator * self, uint8_t branch, CBBlock * block);
/**
 @brief Adds a block to the orphans.
 @param self The BEFullValidator object.
 @param block The block to add.
 @returns true on success and false on error.
 */
bool BEFullValidatorAddBlockToOrphans(BEFullValidator * self, CBBlock * block);
/**
 @brief Does basic validation on a block 
 @param self The BEFullValidator object.
 @param block The block to valdiate.
 @param txHashes 32 byte double Sha-256 hashes for the transactions in the block, one after the other. These will be modified by this function.
 @param networkTime The network time.
 @returns The block status.
 */
BEBlockStatus BEFullValidatorBasicBlockValidation(BEFullValidator * self, CBBlock * block, uint8_t * txHashes, uint64_t networkTime);
/**
 @brief Same as BEFullValidatorBasicBlockValidation but copies the "txHashes" so that the original data is not modified.
 @see BEFullValidatorBasicBlockValidation
 */
BEBlockStatus BEFullValidatorBasicBlockValidationCopy(BEFullValidator * self, CBBlock * block, uint8_t * txHashes, uint64_t networkTime);
/**
 @brief Completes the validation for a block during main branch extention or reorganisation.
 @param self The BEFullValidator object.
 @param branch The branch being validated
 @param block The block to complete validation for.
 @param txHashes 32 byte double Sha-256 hashes for the transactions in the block, one after the other.
 @param height The height of the block.
 @returns BE_BLOCK_VALIDATION_OK if the block passed validation, BE_BLOCK_VALIDATION_BAD if the block failed validation and BE_BLOCK_VALIDATION_ERR on an error.
 */
BEBlockValidationResult BEFullValidatorCompleteBlockValidation(BEFullValidator * self, uint8_t branch, CBBlock * block, uint8_t * txHashes,uint32_t height);
/**
 @brief Ensures a file can be opened.
 @param self The BEFullValidator object.
 */
void BEFullValidatorEnsureCanOpen(BEFullValidator * self);
/**
 @brief Gets the file descriptor for a block file.
 @param self The BEFullValidator object.
 @param fileID The id of the file.
 @param branch The branch to find the block file for.
 @returns The file descriptor on success, or NULL on failure.
 */
FILE * BEFullValidatorGetBlockFile(BEFullValidator * self, uint16_t fileID, uint8_t branch);
/**
 @brief Validates a transaction input.
 @param self The BEFullValidator object.
 @param branch The branch being validated.
 @param block The block begin validated.
 @param blockHeight The height of the block being validated
 @param transactionIndex The index of the transaction to validate.
 @param inputIndex The index of the input to validate.
 @param allSpentOutputs The previous outputs returned from CBTransactionValidateBasic
 @param txHashes 32 byte double Sha-256 hashes for the transactions in the block, one after the other.
 @param value Pointer to the total value of the transaction. This will be incremented by this function with the input value.
 @param sigOps Pointer to the total number of signature operations. This is increased by the signature operations for the input and verified to be less that the maximum allowed signature operations.
 @returns BE_BLOCK_VALIDATION_OK if the transaction passed validation, BE_BLOCK_VALIDATION_BAD if the transaction failed validation and BE_BLOCK_VALIDATION_ERR on an error.
 */
BEBlockValidationResult BEFullValidatorInputValidation(BEFullValidator * self, uint8_t branch, CBBlock * block, uint32_t blockHeight, uint32_t transactionIndex,uint32_t inputIndex, CBPrevOut ** allSpentOutputs, uint8_t * txHashes, uint64_t * value, uint32_t * sigOps);
/**
 @brief Finds a block reference ad returns the index or finds the insertion point if the reference was no found.
 @param lookupTable The table of references to search.
 @param refNum The number of references to search.
 @param hash The hash of the block to search for.
 @param found This is set to true if the reference was found or false otherwise.
 @returns The position of the matching reference in the lookup table or the index of where the reference index should go in the case the reference was not found.
 */
uint32_t BEFullValidatorFindBlockReference(BEBlockReferenceHashIndex * lookupTable, uint32_t refNum, uint8_t * hash, bool * found);
/**
 @brief Finds an output reference ad returns the index or finds the insertion point if the reference was no found.
 @param refs The output references to search.
 @param refNum The number of references to search.
 @param hash The transaction hash of the output to search for.
 @param index The index of the output.
 @param found This is set to true if the reference was found or false otherwise.
 @returns The index of the matching reference or the index of where the reference should go in the case the reference was not found.
 */
uint32_t BEFullValidatorFindOutputReference(BEOutputReference * refs, uint32_t refNum, uint8_t * hash, uint32_t index, bool * found);
/**
 @brief Loads a block from storage.
 @param self The BEFullValidator object.
 @param blockRef A reference to the block in storage.
 @param branch The branch the block belongs to.
 @returns A new CBBlockObject with serailised block data which has not been deserialised or NULL on failure.
 */
CBBlock * BEFullValidatorLoadBlock(BEFullValidator * self, BEBlockReference blockRef, uint32_t branch);
/**
 @brief Loads the validation data for a block-chain branch. This only loads data if the validator file for the branch has not been opened already.
 @param self The BEFullValidator object.
 @param branch The index of the branch to load the data for.
 @returns true of success and false on failure.
 */
bool BEFullValidatorLoadBranchValidator(BEFullValidator * self, uint8_t branch);
/**
 @brief Loads the general validation data. This only loads data if the validator file has not been opened already.
 @param self The BEFullValidator object.
 @returns true of success and false on failure.
 */
bool BEFullValidatorLoadValidator(BEFullValidator * self);
/**
 @brief Processes a block. Block headers are validated, ensuring the integrity of the transaction data is OK, checking the block's proof of work and calculating the total branch work to the genesis block. If the block extends the main branch complete validation is done. If the block extends a branch to become the new main branch because it has the most work, a re-organisation of the block-chain is done.
 @param self The BEFullValidator object.
 @param block The block to process.
 @param networkTime The network time.
 @return The status of the block.
 */
BEBlockStatus BEFullValidatorProcessBlock(BEFullValidator * self, CBBlock * block, uint64_t networkTime);
/**
 @brief Saves the validation data for a branch.
 @param self The BEFullValidator object.
 @param branch The index of the branch to save.
 @returns true of success and false on failure.
 */
bool BEFullValidatorSaveBranchValidator(BEFullValidator * self, uint8_t branch);
/**
 @brief Saves the validator data, does not write orphan data
 @param self The BEFullValidator object.
 @returns true of success and false on failure.
 */
bool BEFullValidatorSaveValidator(BEFullValidator * self);

#endif
