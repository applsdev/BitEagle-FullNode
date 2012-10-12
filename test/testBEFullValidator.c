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
	remove("./validation.dat");
	remove("./branch0.dat");
	remove("./block0-0.dat");
	// Create validator
	BEFullValidator * validator = BENewFullValidator("./", onErrorReceived);
	// Create initial data
	if (NOT BEFullValidatorLoadValidator(validator)){
		printf("VALIDATOR LOAD INIT FAIL\n");
		return 1;
	}
	if (NOT BEFullValidatorLoadBranchValidator(validator,0)){
		printf("VALIDATOR LOAD BRANCH INIT FAIL\n");
		return 1;
	}
	CBReleaseObject(validator);
	// Now create it again. It should load the data.
	validator = BENewFullValidator("./", onErrorReceived);
	if (NOT BEFullValidatorLoadValidator(validator)){
		printf("VALIDATOR LOAD FROM FILE FAIL\n");
		return 1;
	}
	if (NOT BEFullValidatorLoadBranchValidator(validator,0)){
		printf("VALIDATOR LOAD BRANCH FROM FILE FAIL\n");
		return 1;
	}
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
	if(validator->branches[0].lastRetargetTime != 1231006505){
		printf("LAST RETARGET FAIL\n");
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
	if(memcmp(validator->branches[0].referenceTable[0].blockHash,(uint8_t []){0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00},32)){
		printf("GENESIS REF HASH FAIL\n");
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
	if(validator->branches[0].references[0].target != CB_MAX_TARGET){
		printf("REF TARGET FAIL\n");
		return 1;
	}
	if(validator->branches[0].references[0].time != 1231006505){
		printf("REF TIME FAIL\n");
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
	// Test unspent coinbase output
	if (validator->branches->numUnspentOutputs != 1){
		printf("NUM UNSPENT OUTPUTS FAIL\n");
		return 1;
	}
	if (validator->branches->unspentOutputs[0].branch) {
		printf("UNSPENT OUTPUT BRANCH FAIL\n");
		return 1;
	}
	if (NOT validator->branches->unspentOutputs[0].coinbase) {
		printf("UNSPENT OUTPUT COINBASE FAIL\n");
		return 1;
	}
	if (validator->branches->unspentOutputs[0].height) {
		printf("UNSPENT OUTPUT HEIGHT FAIL\n");
		return 1;
	}
	if (memcmp(validator->branches->unspentOutputs[0].outputHash,(uint8_t []){0x3b,0xa3,0xed,0xfd,0x7a,0x7b,0x12,0xb2,0x7a,0xc7,0x2c,0x3e,0x67,0x76,0x8f,0x61,0x7f,0xc8,0x1b,0xc3,0x88,0x8a,0x51,0x32,0x3a,0x9f,0xb8,0xaa,0x4b,0x1e,0x5e,0x4a},32)) {
		printf("UNSPENT OUTPUT HASH FAIL\n");
		return 1;
	}
	if (validator->branches->unspentOutputs[0].ref.fileID) {
		printf("UNSPENT OUTPUT FILE ID FAIL\n");
		return 1;
	}
	if (validator->branches->unspentOutputs[0].ref.filePos != 209) {
		printf("UNSPENT OUTPUT FILE POS FAIL\n");
		return 1;
	}
	// Verify unspent output is correct
	FILE * fd = BEFullValidatorGetBlockFile(validator, 0, 0);
	fseek(fd, 209, SEEK_SET);
	CBByteArray * outputBytes = CBNewByteArrayOfSize(76, onErrorReceived);
	if (fread(CBByteArrayGetData(outputBytes), 1, 76, fd) != 76){
		printf("UNSPENT OUTPUT READ FAIL\n");
		return 1;
	}
	CBTransactionOutput * output = CBNewTransactionOutputFromData(outputBytes, onErrorReceived);
	CBTransactionOutputDeserialise(output);
	if (output->value != 50 * CB_ONE_BITCOIN) {
		printf("UNSPENT OUTPUT VALUE FAIL\n");
		return 1;
	}
	if (output->scriptObject->length != 67) {
		printf("UNSPENT OUTPUT SCRIPT SIZE FAIL\n");
		return 1;
	}
	if (memcmp(CBByteArrayGetData(output->scriptObject),(uint8_t []){0x41,0x04,0x67,0x8A,0xFD,0xB0,0xFE,0x55,0x48,0x27,0x19,
		0x67,0xF1,0xA6,0x71,0x30,0xB7,0x10,0x5C,0xD6,0xA8,0x28,0xE0,0x39,0x09,0xA6,0x79,0x62,0xE0,0xEA,0x1F,0x61,0xDE,0xB6,0x49,0xF6,
		0xBC,0x3F,0x4C,0xEF,0x38,0xC4,0xF3,0x55,0x04,0xE5,0x1E,0xC1,0x12,0xDE,0x5C,0x38,0x4D,0xF7,0xBA,0x0B,0x8D,0x57,0x8A,0x4C,0x70,
		0x2B,0x6B,0xF1,0x1D,0x5F,0xAC},67)) {
			printf("UNSPENT OUTPUT SCRIPT FAIL\n");
			return 1;
	}
	CBReleaseObject(output);
	CBReleaseObject(outputBytes);
	// Try loading the genesis block
	CBBlock * block = BEFullValidatorLoadBlock(validator, validator->branches[0].references[0], 0);
	CBBlockDeserialise(block, true);
	if (NOT block) {
		printf("GENESIS RETRIEVE FAIL\n");
		return 1;
	}
	if (memcmp(CBBlockGetHash(block),(uint8_t []){0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00},32)) {
		printf("GENESIS HASH FAIL\n");
		return 1;
	}
	if (memcmp(CBByteArrayGetData(block->merkleRoot),(uint8_t []){0x3B,0xA3,0xED,0xFD,0x7A,0x7B,0x12,0xB2,0x7A,0xC7,0x2C,0x3E,0x67,0x76,0x8F,0x61,0x7F,0xC8,0x1B,0xC3,0x88,0x8A,0x51,0x32,0x3A,0x9F,0xB8,0xAA,0x4B,0x1E,0x5E,0x4A},32)) {
		printf("GENESIS MERKLE ROOT FAIL\n");
		return 1;
	}
	if (block->nounce != 2083236893) {
		printf("GENESIS NOUNCE FAIL\n");
		return 1;
	}
	if (block->target != CB_MAX_TARGET) {
		printf("GENESIS TARGET FAIL\n");
		return 1;
	}
	if (block->time != 1231006505) {
		printf("GENESIS TIME FAIL\n");
		return 1;
	}
	if (block->transactionNum != 1) {
		printf("GENESIS TRANSACTION NUM FAIL\n");
		return 1;
	}
	if (block->version != 1) {
		printf("GENESIS VERSION FAIL\n");
		return 1;
	}
	if (block->transactions[0]->inputNum != 1) {
		printf("GENESIS COIN BASE INPUT NUM FAIL\n");
		return 1;
	}
	if (block->transactions[0]->outputNum != 1) {
		printf("GENESIS COIN BASE INPUT NUM FAIL\n");
		return 1;
	}
	if (block->transactions[0]->version != 1) {
		printf("GENESIS COIN BASE VERSION FAIL\n");
		return 1;
	}
	if (block->transactions[0]->lockTime) {
		printf("GENESIS COIN BASE LOCK TIME FAIL\n");
		return 1;
	}
	if (block->transactions[0]->lockTime) {
		printf("GENESIS COIN BASE LOCK TIME FAIL\n");
		return 1;
	}
	if (block->transactions[0]->inputs[0]->scriptObject->length != 0x4D) {
		printf("GENESIS TRANSACTION INPUT SCRIPT LENGTH FAIL\n0x");
		return 1;
	}
	if (block->transactions[0]->outputs[0]->scriptObject->length != 0x43) {
		printf("GENESIS TRANSACTION OUTPUT SCRIPT LENGTH FAIL\n0x");
		return 1;
	}
	for (int x = 0; x < 32; x++) {
		if(CBByteArrayGetByte(block->transactions[0]->inputs[0]->prevOut.hash, x) != 0){
			printf("GENESIS TRANSACTION INPUT OUT POINTER HASH FAIL\n");
			return 1;
		}
	}
	if (block->transactions[0]->inputs[0]->prevOut.index != 0xFFFFFFFF) {
		printf("GENESIS TRANSACTION INPUT OUT POINTER INDEX FAIL\n0x");
		return 1;
	}
	if (block->transactions[0]->inputs[0]->sequence != CB_TRANSACTION_INPUT_FINAL) {
		printf("GENESIS TRANSACTION INPUT SEQUENCE FAIL\n0x");
		return 1;
	}
	CBByteArray * genesisInScript = CBNewByteArrayWithDataCopy((uint8_t [77]){0x04,0xFF,0xFF,0x00,0x1D,0x01,0x04,0x45,0x54,0x68,0x65,0x20,0x54,0x69,0x6D,0x65,0x73,0x20,0x30,0x33,0x2F,0x4A,0x61,0x6E,0x2F,0x32,0x30,0x30,0x39,0x20,0x43,0x68,0x61,0x6E,0x63,0x65,0x6C,0x6C,0x6F,0x72,0x20,0x6F,0x6E,0x20,0x62,0x72,0x69,0x6E,0x6B,0x20,0x6F,0x66,0x20,0x73,0x65,0x63,0x6F,0x6E,0x64,0x20,0x62,0x61,0x69,0x6C,0x6F,0x75,0x74,0x20,0x66,0x6F,0x72,0x20,0x62,0x61,0x6E,0x6B,0x73}, 77, onErrorReceived);
	CBByteArray * genesisOutScript = CBNewByteArrayWithDataCopy((uint8_t [67]){0x41,0x04,0x67,0x8A,0xFD,0xB0,0xFE,0x55,0x48,0x27,0x19,0x67,0xF1,0xA6,0x71,0x30,0xB7,0x10,0x5C,0xD6,0xA8,0x28,0xE0,0x39,0x09,0xA6,0x79,0x62,0xE0,0xEA,0x1F,0x61,0xDE,0xB6,0x49,0xF6,0xBC,0x3F,0x4C,0xEF,0x38,0xC4,0xF3,0x55,0x04,0xE5,0x1E,0xC1,0x12,0xDE,0x5C,0x38,0x4D,0xF7,0xBA,0x0B,0x8D,0x57,0x8A,0x4C,0x70,0x2B,0x6B,0xF1,0x1D,0x5F,0xAC}, 67, onErrorReceived);
	if (CBByteArrayCompare(block->transactions[0]->inputs[0]->scriptObject, genesisInScript)) {
		printf("GENESIS IN SCRIPT FAIL\n0x");
		return 1;
	}
	if (block->transactions[0]->outputs[0]->value != 5000000000) {
		printf("GENESIS TRANSACTION OUTPUT VALUE FAIL\n0x");
		return 1;
	}
	if (CBByteArrayCompare(block->transactions[0]->outputs[0]->scriptObject, genesisOutScript)) {
		printf("GENESIS OUT SCRIPT FAIL\n0x");
		return 1;
	}
	CBReleaseObject(genesisInScript);
	CBReleaseObject(genesisOutScript);
	CBReleaseObject(block);
	// Test validating a correct block onto the genesis block
	CBBlock * block1 = CBNewBlock(onErrorReceived);
	block1->version = 1;
	block1->prevBlockHash = CBNewByteArrayWithDataCopy((uint8_t []){0x6F,0xE2,0x8C,0x0A,0xB6,0xF1,0xB3,0x72,0xC1,0xA6,0xA2,0x46,0xAE,0x63,0xF7,0x4F,0x93,0x1E,0x83,0x65,0xE1,0x5A,0x08,0x9C,0x68,0xD6,0x19,0x00,0x00,0x00,0x00,0x00}, 32, onErrorReceived);
	block1->merkleRoot = CBNewByteArrayWithDataCopy((uint8_t []){0x98,0x20,0x51,0xfD,0x1E,0x4B,0xA7,0x44,0xBB,0xBE,0x68,0x0E,0x1F,0xEE,0x14,0x67,0x7B,0xA1,0xA3,0xC3,0x54,0x0B,0xF7,0xB1,0xCD,0xB6,0x06,0xE8,0x57,0x23,0x3E,0x0E}, 32, onErrorReceived);
	block1->time = 1231469665;
	block1->target = CB_MAX_TARGET;
	block1->nounce = 2573394689;
	block1->transactionNum = 1;
	block1->transactions = malloc(sizeof(*block1->transactions));
	block1->transactions[0] = CBNewTransaction(0, 1, onErrorReceived);
	CBByteArray * nullHash = CBNewByteArrayOfSize(32, onErrorReceived);
	memset(CBByteArrayGetData(nullHash), 0, 32);
	CBScript * script = CBNewScriptWithDataCopy((uint8_t []){0x04,0xff,0xff,0x00,0x1d,0x01,0x04}, 7, onErrorReceived);
	CBTransactionTakeInput(block1->transactions[0], CBNewTransactionInput(script, CB_TRANSACTION_INPUT_FINAL, nullHash, 0xFFFFFFFF, onErrorReceived));
	CBReleaseObject(script);
	CBReleaseObject(nullHash);
	script = CBNewScriptWithDataCopy((uint8_t []){0x41,0x04,0x96,0xb5,0x38,0xe8,0x53,0x51,0x9c,0x72,0x6a,0x2c,0x91,0xe6,0x1e,0xc1,0x16,0x00,0xae,0x13,0x90,0x81,0x3a,0x62,0x7c,0x66,0xfb,0x8b,0xe7,0x94,0x7b,0xe6,0x3c,0x52,0xda,0x75,0x89,0x37,0x95,0x15,0xd4,0xe0,0xa6,0x04,0xf8,0x14,0x17,0x81,0xe6,0x22,0x94,0x72,0x11,0x66,0xbf,0x62,0x1e,0x73,0xa8,0x2c,0xbf,0x23,0x42,0xc8,0x58,0xee,0xAC}, 67, onErrorReceived);
	CBTransactionTakeOutput(block1->transactions[0], CBNewTransactionOutput(5000000000, script, onErrorReceived));
	CBReleaseObject(script);
	CBGetMessage(block1)->bytes = CBNewByteArrayOfSize(CBBlockCalculateLength(block1, true), onErrorReceived);
	CBBlockSerialise(block1, true);
	// Made block now proceed with the test.
	printf("%lli\n",CB_MAX_MONEY);
	BEBlockStatus res = BEFullValidatorProcessBlock(validator, block1, 1349643202);
	if (res != BE_BLOCK_STATUS_MAIN) {
		printf("MAIN CHAIN ADD FAIL\n");
		return 1;
	}
	// Test duplicate add.
	CBReleaseObject(block1);
	CBReleaseObject(validator);
	return 0;
}
