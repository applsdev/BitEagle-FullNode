//
//  testBEFullValidator.c
//  BitEagle-FullNode
//
//  Created by Matthew Mitchell on 17/09/2012.
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

#include "BEFullValidator.h"
#include <stdarg.h>

void onErrorReceived(CBError a,char * format,...);
void onErrorReceived(CBError a,char * format,...){
	va_list argptr;
    va_start(argptr, format);
    vfprintf(stderr, format, argptr);
    va_end(argptr);
	printf("\n");
}

int main(){
	char validatorFile[3 + strlen(BE_VALIDATION_DATA_FILE)];
	validatorFile[0] = '.';
	validatorFile[1] = '/';
	strcpy(validatorFile, BE_VALIDATION_DATA_FILE);
	remove(validatorFile);
	// Create validator
	BEFullValidator * validator = BENewFullValidator("./", onErrorReceived);
	CBReleaseObject(validator);
	// Now create it again. It should load the data.
	validator = BENewFullValidator("./", onErrorReceived);
	// Now verify that the data is correct.
	if(validator->numOrphans){
		printf("ORPHAN NUM FAIL\n");
		return 1;
	}
	if(validator->numBranches != 1){
		printf("BRANCH NUM FAIL\n");
		return 1;
	}
	if(validator->mainBranch != 0){
		printf("MAIN BRANCH FAIL\n");
		return 1;
	}
	if(validator->numUnspentOutputs){
		printf("NUM UNSPENT OUTPUTS FAIL\n");
		return 1;
	}
	if(validator->branches[0].lastBlock){
		printf("LAST BLOCK FAIL\n");
		return 1;
	}
	if(validator->branches[0].lastRetarget){
		printf("LAST RETARGET FAIL\n");
		return 1;
	}
	if(validator->branches[0].lastTarget != CB_MAX_TARGET){
		printf("LAST TARGET FAIL\n");
		return 1;
	}
	if(validator->branches[0].numRefs != 1){
		printf("NUM REFS FAIL\n");
		return 1;
	}
	if(validator->branches[0].parentBranch){
		printf("PARENT BRANCH FAIL\n");
		return 1;
	}
	for (uint8_t x = 0; x < 6; x++) {
		if(validator->branches[0].prevTime[x] != 1231006505){
			printf("PREV TIME %u FAIL\n",x);
			return 1;
		}
	}
	if(memcmp(validator->branches[0].references[0].blockHash,(uint8_t []){0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00},32)){
		printf("GENESIS HASH FAIL\n");
		return 1;
	}
	if(validator->branches[0].references[0].ref.fileID){
		printf("FILE ID FAIL\n");
		return 1;
	}
	if(validator->branches[0].references[0].ref.filePos){
		printf("FILE POS FAIL\n");
		return 1;
	}
	if(validator->branches[0].startHeight){
		printf("START HEIGHT FAIL\n");
		return 1;
	}
	if(validator->branches[0].work.length != 1){
		printf("WORK LENGTH FAIL\n");
		return 1;
	}
	if(validator->branches[0].work.data[0]){
		printf("WORK VAL FAIL\n");
		return 1;
	}
	CBReleaseObject(validator);
	return 0;
}
