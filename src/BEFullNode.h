//
//  BEFullNode.h
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
 @brief Downloads and validates the entire bitcoin block-chain.
 */

#ifndef BEFULLNODEH
#define BEFULLNODEH

#include "BEConstants.h"
#include "CBNetworkCommunicator.h"
#include <pwd.h>
#include <unistd.h>
#include <stdio.h>

/**
 @brief Structure for BEFullNode objects. @see BEFullNode.h
 */
typedef struct{
	CBNetworkCommunicator base;
	FILE * addressFile;
} BEFullNode;

/**
 @brief Creates a new BEFullNode object.
 @returns A new BEFullNode object.
 */
BEFullNode * BENewFullNode(void (*onErrorReceived)(CBError error,char *,...));

/**
 @brief Gets a BEFullNode from another object. Use this to avoid casts.
 @param self The object to obtain the BEFullNode from.
 @returns The BEFullNode object.
 */
BEFullNode * BEGetFullNode(void * self);

/**
 @brief Initialises a BEFullNode object
 @param self The BEFullNode object to initialise
 @returns true on success, false on failure.
 */
bool BEInitFullNode(BEFullNode * self, void (*onErrorReceived)(CBError error,char *,...));

/**
 @brief Frees a BEFullNode object.
 @param self The BEFullNode object to free.
 */
void BEFreeFullNode(void * self);

// Functions

/**
 @brief Handles an onBadTime event.
 @param self The BEFullNode object.
 */
void BEFullNodeOnBadTime(void * self);

#endif
