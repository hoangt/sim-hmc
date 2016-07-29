/*********************************************************************************
*  Copyright (c) 2010-2011, Elliott Cooper-Balis
*                             Paul Rosenfeld
*                             Bruce Jacob
*                             University of Maryland 
*                             dramninjas [at] gmail [dot] com
*  All rights reserved.
*  
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions are met:
*  
*     * Redistributions of source code must retain the above copyright notice,
*        this list of conditions and the following disclaimer.
*  
*     * Redistributions in binary form must reproduce the above copyright notice,
*        this list of conditions and the following disclaimer in the documentation
*        and/or other materials provided with the distribution.
*  
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
*  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
*  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
*  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
*  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*********************************************************************************/

#ifndef LOGICLAYERINTERFACE_H
#define LOGICLAYERINTERFACE_H


#include "Transaction.h"
#include "DRAMChannel.h"
#include "LogicOperation.h"
#include <deque>

using namespace std;

namespace BOBSim
{
class LogicLayerInterface
{
public:
    LogicLayerInterface(uint id, DRAMChannel *_channel);

    void ReceiveLogicOperation(Transaction *trans);
    void Update(void);

    DRAMChannel *channel;

	uint simpleControllerID;
	uint64_t currentClockCycle;
	Transaction *currentTransaction; //the current operation the logic layer is working on
	LogicOperation *currentLogicOperation; //holds on to current logic operation object
	deque<Transaction *> pendingLogicOpsQueue;
	vector<Transaction *> newOperationQueue; //incoming LOGIC_OPERATION transactions
	vector<Transaction *> outgoingQueue; //requests or responses generated from LOGIC_OPERATION transaction
};
}

#endif
