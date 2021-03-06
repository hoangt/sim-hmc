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

#ifndef BUSPACKET_H
#define BUSPACKET_H

//Bus Packet header

#include "bob_globals.h"

namespace BOBSim
{
enum BusPacketType
{
	READ,
	READ_P,
	WRITE,
	WRITE_P,
	ACTIVATE,
	REFRESH,
	PRECHARGE,
	READ_DATA,
	WRITE_DATA
};

class BusPacket
{
public:
    //Functions
    BusPacket(BusPacketType packtype, unsigned id, unsigned col, unsigned rw,
              unsigned rnk, unsigned bnk, unsigned prt,
              unsigned mappedChannel, uint64_t addr, bool fromLogic, unsigned bl, unsigned rbl = 0) :
      busPacketType(packtype),
      transactionID(id),
//      column(col),
      row(rw),
      rank(rnk),
      bank(bnk),
      port(prt),
#ifdef HMCSIM_SUPPORT
      burstLen(bl),
      retBurstLen(rbl),
#else
      burstLength(bl),
#endif
      channel(mappedChannel),
      address(addr),
      fromLogicOp(fromLogic)
#ifndef BOBSIM_NO_LOG
      , queueWaitTime(0)
#endif
    {}

    //Fields
    BusPacketType busPacketType;
    unsigned transactionID;
//    unsigned column;
    unsigned row;
    unsigned rank;
    unsigned bank;
    unsigned port;

#ifdef HMCSIM_SUPPORT
    unsigned burstLen;
    unsigned retBurstLen;
#else
    unsigned burstLength;
#endif
	unsigned channel;
    uint64_t address;
    bool fromLogicOp;
#ifndef BOBSIM_NO_LOG
    unsigned queueWaitTime;
#endif

#ifdef HMCSIM_SUPPORT
    void *payload;
#endif

    unsigned reqBurstSize(void)
    {
#ifdef HMCSIM_SUPPORT
      return burstLen;
#else
      return burstLength;
#endif
    }
    unsigned respBurstSize(void)
    {
#ifdef HMCSIM_SUPPORT
      return retBurstLen;
#else
      return burstLength;
#endif
    }
};
}

#endif
