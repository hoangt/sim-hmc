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

#ifndef DRAMCHANNEL_H
#define DRAMCHANNEL_H

//DRAM Channel header

#include "bob_simplecontroller.h"
#include "bob_logiclayerinterface.h"
#include <deque>
#include <vector>

namespace BOBSim
{
class BOB;
class Rank;
class Transaction;

class DRAMChannel
{
private:
    //Ranks of DRAM
    std::vector<Rank*> ranks;
    //Logic chip
    LogicLayerInterface logicLayer;
    // bob interface
    BOB *bob;
    //Command packet being sent on DRAM command bus
    BusPacket *inFlightCommandPacket;
    //Time to send DRAM command packet
    unsigned inFlightCommandCountdown;

    //Data packet being sent on the DRAM data bus
    BusPacket *inFlightDataPacket;
    //Time to send DRAM data packet
    unsigned inFlightDataCountdown;

public:
    //Functions
    DRAMChannel(unsigned id, BOB *_bob, unsigned ranks, unsigned deviceWidth);
    ~DRAMChannel(void);
    bool AddTransaction(Transaction *trans);
    void Update(void);
    void ReceiveOnDataBus(BusPacket *busPacket, bool is_return);
    void ReceiveOnCmdBus(BusPacket *busPacket);

    //Fields
    //Controller used to operate ranks of DRAM
    SimpleController simpleController;
    //This channel's ID in relation to the entire system
    unsigned channelID;
    //Pending outgoing logic response
    Transaction *pendingLogicResponse;

    //Bookkeeping for maximum number of requests waiting in queue
#ifndef BOBSIM_NO_LOG
    unsigned readReturnQueueMax;
#endif
    //Storage for pending response data
    deque<BusPacket*> readReturnQueue;

    //Number of cycles there is no data on the DRAM bus
#ifndef BOBSIM_NO_LOG
    unsigned DRAMBusIdleCount;
#endif
};
}

#endif
