//
//  BEConstants.h
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

#ifndef BECONSTANTSH
#define BECONSTANTSH

//  Macros

#define BE_DATA_DIRECTORY "/.BitEagle_FullNode_Data/"
#define BE_ADDRESS_DATA_FILE "addresses.dat"
#define BE_VALIDATION_DATA_FILE "validation.dat"
#define BE_MAX_ORPHAN_CACHE 3
#define BE_MAX_BRANCH_CACHE 4
#define BEHashMiniKey(hash) (uint64_t)hash[31] << 56 | (uint64_t)hash[30] << 48 | (uint64_t)hash[29] << 40 | (uint64_t)hash[28] << 32 | (uint64_t)hash[27] << 24 | (uint64_t)hash[26] << 16 | (uint64_t)hash[25] << 8 | (uint64_t)hash[24]
#define BE_MIN(a,b) ((a) < (b) ? a : b)
#define BE_MAX(a,b) ((a) > (b) ? a : b)

// Enums

/**
 @brief The return type for BEFullValidatorProcessBlock
 */
typedef enum{
	BE_BLOCK_STATUS_MAIN, /**< The block has extended the main branch. */
	BE_BLOCK_STATUS_SIDE, /**< The block has extended a branch which is not the main branch. */
	BE_BLOCK_STATUS_ORPHAN, /**< The block is an orphan */
	BE_BLOCK_STATUS_BAD, /**< The block is not ok. */
	BE_BLOCK_STATUS_BAD_TIME, /**< The block has a bad time */
	BE_BLOCK_STATUS_DUPLICATE, /**< The block has already been received. */
	BE_BLOCK_STATUS_ERROR, /**< There was an error while processing a block */
	BE_BLOCK_STATUS_MAX_CACHE, /**< Reached the maximum allowed branches or orphans. */
	BE_BLOCK_STATUS_CONTINUE, /**< Continue with the validation */
} BEBlockStatus;

/**
 @brief The return type for BEFullValidatorCompleteBlockValidation
 */
typedef enum{
	BE_BLOCK_VALIDATION_OK, /**< The validation passed with no problems. */
	BE_BLOCK_VALIDATION_BAD, /**< The block failed valdiation. */
	BE_BLOCK_VALIDATION_ERR, /**< There was an error during the validation processing. */
} BEBlockValidationResult;

#endif
