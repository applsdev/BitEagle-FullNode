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
	uint32_t outputIndex; /** The index for the output */
	BEFileReference ref; /**< The file reference for the output */
}BEOutputReference;

/**
 @brief References a block in the block storage.
 */
typedef struct{
	uint8_t blockHash[32]; /**< The block hash. */
	BEFileReference ref; /**< The file reference for the block */
}BEBlockReference;

/**
 @brief Represents a block branch.
 */
typedef struct{
	uint32_t numRefs; /**< The number of block references in the branch */
	BEBlockReference * references; /**< The block references */
	uint32_t lastBlock; /**< Index of the last block in the branch */
	uint32_t lastRetarget; /**< Index of the last block to have a retarget */
	uint32_t lastTarget; /**< The target for the last block */
	uint64_t prevTime[6]; /**< A cache of the last 6 timestamps. A new block on the chain must be at or above the timestamp 6 blocks back. */
	uint8_t parentBranch; /**< The branch this branch is connected to. */
	uint32_t startHeight; /**< The starting height where this branch begins */
	CBBigInt work; /**< The total work for this branch. The branch with the highest work is the winner! */
} BEBlockBranch;

/**
 @brief A FILE object for block storage and the index of this file.
 */
typedef struct{
	FILE * file;
	uint16_t fileID;
}BEBLockFile;

/**
 @brief Structure for BEFullValidator objects. @see BEFullValidator.h
 */
typedef struct{
	CBObject base;
	FILE * validatorFile; /**< The file for the validation data */
	BEBLockFile * blockFiles; /**< Open block files */
	uint16_t numOpenBlockFiles; /**< Number of open block files */
	uint16_t fileDescLimit; /**< Maximum number of allowed file descriptors */
	uint8_t numOrphans; /**< The number of orhpans */
	BEBlockReference orphans[BE_MAX_ORPHAN_CACHE]; /**< The ophan block references */
	uint8_t mainBranch; /**< The index for the main branch */
	uint8_t numBranches; /**< The number of block-chain branches. Cannot exceed BE_MAX_BRANCH_CACHE */
	BEBlockBranch branches[BE_MAX_BRANCH_CACHE]; /**< The block-chain branches. */
	uint32_t numUnspentOutputs; /**< The number of unspent outputs */
	BEOutputReference * unspentOutputs; /**< A list of unspent outputs */
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
 @brief Initialises a BEFullValidator object
 @param self The BEFullValidator object to initialise
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
 @brief Returns the BEBlockReference for a hash from a branch if it exists.
 @param branch The branch to search.
 @param hash The hash of the block to search for. Must be 32 bytes.
 @returns a BEBlockReference if one exists or NULL.
 */
BEBlockReference * BEBlockBranchFindBlock(BEBlockBranch * branch, uint8_t * hash);
/**
 @brief Processes a block. Block headers are validated, ensuring the integrity of the transaction data is OK, checking the block's proof of work and calculating the total branch work to the genesis block. If the block extends the main branch complete validation is done. If the block extends a branch to become the new main branch because it has the most work, a re-organisation of the block-chain is done.
 @param self The BEFullValidator object.
 @param block The block to process.
 @param networkTime The network time.
 @return The status of the block.
 */
BEBlockStatus BEFullValidatorProcessBlock(BEFullValidator * self, CBBlock * block, uint64_t networkTime);

#endif
