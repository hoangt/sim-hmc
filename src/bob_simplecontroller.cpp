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

//Simple Controller source

#include <cmath>
#include <cstring>
#include "../include/bob_simplecontroller.h"
#include "../include/bob_dramchannel.h"
#include "../include/bob_buspacket.h"
#include "../include/bob_bankstate.h"

using namespace std;
using namespace BOBSim;

SimpleController::SimpleController(DRAMChannel *parent) :
    refreshCounter(0),
	readCounter(0),
	writeCounter(0),
	commandQueueMax(0),
	commandQueueAverage(0),
	numIdleBanksAverage(0),
	numActBanksAverage(0),
	numPreBanksAverage(0),
	numRefBanksAverage(0),
	RRQFull(0),
    waitingACTS(0),
    channel(parent), //Registers the parent channel object
	rankBitWidth(log2(NUM_RANKS)),
	bankBitWidth(log2(NUM_BANKS)),
	rowBitWidth(log2(NUM_ROWS)),
	colBitWidth(log2(NUM_COLS)),
	busOffsetBitWidth(log2(BUS_ALIGNMENT_SIZE)),
	channelBitWidth(log2(NUM_CHANNELS)),
	cacheOffset(log2(CACHE_LINE_SIZE)),
    outstandingReads(0),
    currentClockCycle(0)

{
    //Make the bank state objects
    this->bankStates = new BankState*[NUM_RANKS];
    this->tFAWWindow = new vector<unsigned>[NUM_RANKS];
    this->backgroundEnergy = new uint64_t[NUM_RANKS];
    this->burstEnergy = new uint64_t[NUM_RANKS];
    this->actpreEnergy = new uint64_t[NUM_RANKS];
    this->refreshEnergy = new uint64_t[NUM_RANKS];
    for(unsigned i=0; i<NUM_RANKS; i++)
    {
        this->bankStates[i] = new BankState[NUM_BANKS];
        memset(this->bankStates[i], 0, sizeof(BankState) * NUM_BANKS);

        //init refresh counters
        refreshCounters.push_back(((7800/tCK)/NUM_RANKS)*(i+1));

        //init power fields
        backgroundEnergy[i] = 0;
        burstEnergy[i] = 0;
        actpreEnergy[i] = 0;
        refreshEnergy[i] = 0;
    }
}

SimpleController::~SimpleController(void)
{
  for(unsigned i=0; i<NUM_RANKS; i++)
  {
    delete[] this->bankStates[i];
  }
  delete[] this->bankStates;
  delete[] this->tFAWWindow;
  delete[] backgroundEnergy;
  delete[] burstEnergy;
  delete[] actpreEnergy;
  delete[] refreshEnergy;
  for(deque<BusPacket*>::iterator it = commandQueue.begin(); it != commandQueue.end(); ++it)
  {
    delete *it;
  }
  for(vector< pair<unsigned, BusPacket*> >::iterator it = writeBurst.begin(); it != writeBurst.end(); ++it)
  {
    delete (*it).second;
  }
}

//Updates the state of everything 
void SimpleController::Update(void)
{
	//
	//Stats
	//
	unsigned currentCount = 0;
	//count all the ACTIVATES waiting in the queue
	for(unsigned i=0; i<commandQueue.size(); i++)
	{
        currentCount += (commandQueue[i]->busPacketType==ACTIVATE);
	}
	if(currentCount>commandQueueMax) commandQueueMax = currentCount;

	//cumulative rolling average
	commandQueueAverage += currentCount;

	for(unsigned r=0; r<NUM_RANKS; r++)
    {
		for(unsigned b=0; b<NUM_BANKS; b++)
        {
            switch(bankStates[r][b].currentBankState)
			{
            //count the number of idle banks
            case IDLE:
              numIdleBanksAverage++;
              break;

            //count the number of active banks
            case ROW_ACTIVE:
              numActBanksAverage++;
              break;

            //count the number of precharging banks
            case PRECHARGING:
              numPreBanksAverage++;
              break;

            //count the number of refreshing banks
            case REFRESHING:
              numRefBanksAverage++;
              break;
            }
        }

        //
        //Power
        //
        bool bankOpen = false;
        for(unsigned b=0; b<NUM_BANKS; b++)
		{
          if( (bankOpen = (bankStates[r][b].currentBankState == ROW_ACTIVE ||
                           bankStates[r][b].currentBankState == REFRESHING)) )
            break;
        }

        //DRAM_BUS_WIDTH/2 because value accounts for DDR
        backgroundEnergy[r] += (bankOpen ? IDD3N : IDD2N) * ((DRAM_BUS_WIDTH/2 * 8) / DEVICE_WIDTH);

        //
        //Update
        //
        //Updates the sliding window for tFAW
		for(unsigned i=0; i<tFAWWindow[r].size(); i++)
        {
            if(!--tFAWWindow[r][i])
                tFAWWindow[r].erase(tFAWWindow[r].begin());
		}

        //Updates the bank states for each rank
		for(unsigned b=0; b<NUM_BANKS; b++)
        {
            bankStates[r][b].UpdateStateChange();
        }

        //Handle refresh counters
        refreshCounters[r] -= (refreshCounters[r]>0);
    }

	//Send write data to data bus
    for(unsigned i=0; i<writeBurst.size(); i++)
	{
        writeBurst[i].first--;
	}
    if(writeBurst.size()>0&&(*writeBurst.begin()).first==0)
	{
        if(DEBUG_CHANNEL) DEBUG("     == Sending Write Data : ");
        channel->ReceiveOnDataBus((*writeBurst.begin()).second);
        writeBurst.erase(writeBurst.begin());
	}

    bool issuingRefresh = false;

    //Figure out if everyone who needs a refresh can actually receive one
	for(unsigned r=0; r<NUM_RANKS; r++)
	{
        if( ! refreshCounters[r])
		{
			if(DEBUG_CHANNEL) DEBUG("      !! -- Rank "<<r<<" needs refresh");
			//Check to be sure we can issue a refresh
            bool canIssueRefresh = true;
			for(unsigned b=0; b<NUM_BANKS; b++)
			{
                if(bankStates[r][b].nextActivate > currentClockCycle ||
                   bankStates[r][b].currentBankState != IDLE)
				{
                    canIssueRefresh = false;
                    break;
				}
			}

			//Once all counters have reached 0 and everyone is either idle or ready to accept refresh-CAS
			if(canIssueRefresh)
			{
				if(DEBUG_CHANNEL) DEBUGN("-- !! Refresh is issuable - Sending : ");

				//BusPacketType packtype, unsigned transactionID, unsigned col, unsigned rw, unsigned r, unsigned b, unsigned prt, unsigned bl
                BusPacket *refreshPacket = new BusPacket(REFRESH, -1, 0, 0, r, 0, 0, 0, channel->channelID, 0, false);

				//Send to command bus
                channel->ReceiveOnCmdBus(refreshPacket);

                //make sure we don't send anythign else
                issuingRefresh = true;

				refreshEnergy[r] += (IDD5B-IDD3N) * tRFC * ((DRAM_BUS_WIDTH/2 * 8) / DEVICE_WIDTH);

				for(unsigned b=0; b<NUM_BANKS; b++)
                {
                    bankStates[r][b].currentBankState = REFRESHING;
                    bankStates[r][b].stateChangeCountdown = tRFC;
                    bankStates[r][b].nextActivate = currentClockCycle + tRFC;
                    bankStates[r][b].lastCommand = REFRESH;
				}

				//reset refresh counters
				for(unsigned i=0; i<NUM_RANKS; i++)
				{
                    if( ! refreshCounters[r])
					{
						refreshCounters[r] = 7800/tCK;
					}
				}

				//only issue one
				break;
			}
		}
    }

	//If no refresh is being issued then do this block
	if(!issuingRefresh)
	{
        for(unsigned i=0; i<commandQueue.size(); i++)
        {
            //make sure we don't send a command ahead of its own ACTIVATE
            if( ( ! (i>0 &&
                     commandQueue[i]->transactionID == commandQueue[i-1]->transactionID)) &&
                IsIssuable(commandQueue[i])) //Checks to see if this particular request can be issued
            {
				//send to channel
                this->channel->ReceiveOnCmdBus(commandQueue[i]);

				//update channel controllers bank state bookkeeping
				unsigned rank = commandQueue[i]->rank;
				unsigned bank = commandQueue[i]->bank;

				//
				//Main block for determining what to do with each type of command
                //
                BankState *bankstate = &bankStates[rank][bank];
				switch(commandQueue[i]->busPacketType)
				{
                case READ_P:
					outstandingReads++;
					waitingACTS--;
					if(waitingACTS<0)
					{
						ERROR("#@)($J@)#(RJ");
						exit(0);
					}

					//keep track of energy
					burstEnergy[rank] += (IDD4R - IDD3N) * BL/2 * ((DRAM_BUS_WIDTH/2 * 8) / DEVICE_WIDTH);

                    bankstate->lastCommand = commandQueue[i]->busPacketType;
                    bankstate->stateChangeCountdown = (4*tCK>7.5)?tRTP:ceil(7.5/tCK); //4 clk or 7.5ns
                    bankstate->nextActivate = max(bankstate->nextActivate, currentClockCycle + tRTP + tRP);
//					bankstate->nextRefresh = currentClockCycle + tRTP + tRP;

					for(unsigned r=0; r<NUM_RANKS; r++)
                    {
                        uint64_t read_offset;
                        if(r==rank)
                          read_offset = max((uint)tCCD, commandQueue[i]->burstLength);
                        else
                          read_offset = commandQueue[i]->burstLength + tRTRS;

                        for(unsigned b=0; b<NUM_BANKS; b++)
                        {
                            bankStates[r][b].nextRead = max(bankStates[r][b].nextRead,
                                                            currentClockCycle + read_offset); // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
                            bankStates[r][b].nextWrite = max(bankStates[r][b].nextWrite,
                                                             currentClockCycle + (tCL + commandQueue[i]->burstLength + tRTRS - tCWL)); // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
                        }
					}

					//prevents read or write being issued while waiting for auto-precharge to close page
                    bankstate->nextRead = bankstate->nextActivate;
                    bankstate->nextWrite = bankstate->nextActivate;
					break;
				case WRITE_P:
                {
					waitingACTS--;
					if(waitingACTS<0)
					{
						ERROR(")(JWE)(FJEWF");
						exit(0);
					}

					//keep track of energy
					burstEnergy[rank] += (IDD4W - IDD3N) * BL/2 * ((DRAM_BUS_WIDTH/2 * 8) / DEVICE_WIDTH);

                    BusPacket *writeData = new BusPacket(*commandQueue[i]);
					writeData->busPacketType = WRITE_DATA;
                    writeBurst.push_back( make_pair(tCWL, writeData) );
                    if(DEBUG_CHANNEL) DEBUG("     !!! After Issuing WRITE_P, burstQueue is :"<<writeBurst.size()<<" "<<writeBurst.size()<<" with head : "<<(*writeBurst.begin()).second);

                    bankstate->lastCommand = commandQueue[i]->busPacketType;
                    unsigned stateChangeCountdown = tCWL + commandQueue[i]->burstLength + tWR;
                    bankstate->stateChangeCountdown = stateChangeCountdown; // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
                    bankstate->nextActivate = currentClockCycle + stateChangeCountdown + tRP; // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
//					bankstate->nextRefresh = bankstate->nextActivate; // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH

					for(unsigned r=0; r<NUM_RANKS; r++)
					{
						if(r==rank)
						{
							for(unsigned b=0; b<NUM_BANKS; b++)
							{
                                bankStates[r][b].nextRead = max(bankStates[r][b].nextRead,
                                                                currentClockCycle + commandQueue[i]->burstLength + tCWL + tWTR); // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
                                bankStates[r][b].nextWrite = max(bankStates[r][b].nextWrite,
                                                                 currentClockCycle+(uint64_t)max((uint)tCCD, commandQueue[i]->burstLength)); // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
							}
						}
						else
						{
							for(unsigned b=0; b<NUM_BANKS; b++)
							{
                                bankStates[r][b].nextRead = max(bankStates[r][b].nextRead,
                                                                currentClockCycle + commandQueue[i]->burstLength + tRTRS + tCWL - tCL); // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
                                bankStates[r][b].nextWrite = max(bankStates[r][b].nextWrite,
                                                                 currentClockCycle + commandQueue[i]->burstLength + tRTRS); // commandQueue[i]->burstLength == TRANSACTION_SIZE/DRAM_BUS_WIDTH
							}
						}
					}

					//prevents read or write being issued while waiting for auto-precharge to close page
                    bankstate->nextRead = bankstate->nextActivate;
                    bankstate->nextWrite = bankstate->nextActivate;
					break;
                }
                case ACTIVATE:
					for(unsigned b=0; b<NUM_BANKS; b++)
					{
						if(b!=bank)
						{
							bankStates[rank][b].nextActivate = max(currentClockCycle + tRRD, bankStates[rank][b].nextActivate);
						}
                    }

					actpreEnergy[rank] += ((IDD0 * tRC) - ((IDD3N * tRAS) + (IDD2N * (tRC - tRAS)))) * ((DRAM_BUS_WIDTH/2 * 8) / DEVICE_WIDTH);

                    bankstate->lastCommand = commandQueue[i]->busPacketType;
                    bankstate->currentBankState = ROW_ACTIVE;
                    bankstate->openRowAddress = commandQueue[i]->row;
                    bankstate->nextActivate = currentClockCycle + tRC;
                    bankstate->nextRead = max(currentClockCycle + tRCD, bankstate->nextRead);
                    bankstate->nextWrite = max(currentClockCycle + tRCD, bankstate->nextWrite);

					//keep track of sliding window
					tFAWWindow[rank].push_back(tFAW);

					break;
				default:
                    ERROR("Unexpected packet type");
					abort();
				}

                //erase
                commandQueue.erase(commandQueue.begin()+i);

                break;
			}
		}
    }

	//increment clock cycle
	currentClockCycle++;
}


bool SimpleController::IsIssuable(BusPacket *busPacket)
{
	unsigned rank = busPacket->rank;
	unsigned bank = busPacket->bank;

	//if((channel->readReturnQueue.size()+outstandingReads) * TRANSACTION_SIZE >= CHANNEL_RETURN_Q_MAX)
	//if((channel->readReturnQueue.size()) * TRANSACTION_SIZE >= CHANNEL_RETURN_Q_MAX)
	//	{
	//RRQFull++;
	//DEBUG("!!!!!!!!!"<<*busPacket)
	//exit(0);
	//return false;
	//}
	switch(busPacket->busPacketType)
	{
    case READ_P:
		if(bankStates[rank][bank].currentBankState == ROW_ACTIVE &&
           bankStates[rank][bank].openRowAddress == busPacket->row &&
           currentClockCycle >= bankStates[rank][bank].nextRead &&
           (channel->readReturnQueue.size()+outstandingReads) * (busPacket->burstLength * DRAM_BUS_WIDTH) < CHANNEL_RETURN_Q_MAX) // busPacket->burstLength * DRAM_BUS_WIDTH == TRANSACTION_SIZE
        {
			return true;
		}
		else
		{
            if((channel->readReturnQueue.size()+outstandingReads) * (busPacket->burstLength * DRAM_BUS_WIDTH) >= CHANNEL_RETURN_Q_MAX) // busPacket->burstLength * DRAM_BUS_WIDTH == TRANSACTION_SIZE
			{
				RRQFull++;
			}
			return false;
        }

        break;
    case WRITE_P:
		if(bankStates[rank][bank].currentBankState == ROW_ACTIVE &&
           bankStates[rank][bank].openRowAddress == busPacket->row &&
           currentClockCycle >= bankStates[rank][bank].nextWrite &&
           (channel->readReturnQueue.size()+outstandingReads) * (busPacket->burstLength * DRAM_BUS_WIDTH) < CHANNEL_RETURN_Q_MAX) // busPacket->burstLength * DRAM_BUS_WIDTH == TRANSACTION_SIZE
        {
			return true;
		}
		else
		{
            if((channel->readReturnQueue.size()+outstandingReads) * (busPacket->burstLength * DRAM_BUS_WIDTH) >= CHANNEL_RETURN_Q_MAX) // busPacket->burstLength * DRAM_BUS_WIDTH == TRANSACTION_SIZE
			{
				RRQFull++;
			}
			return false;
        }
		break;
    case ACTIVATE:
        return (bankStates[rank][bank].currentBankState == IDLE &&
                currentClockCycle >= bankStates[rank][bank].nextActivate &&
		        refreshCounters[rank]>0 &&
                tFAWWindow[rank].size()<4);
	default:
        ERROR("== Error - Checking issuability on unknown packet type");
		exit(0);
    }
}

void SimpleController::AddTransaction(Transaction *trans)
{
	//map physical address to rank/bank/row/col
    unsigned mappedRank, mappedBank, mappedRow, mappedCol;
    AddressMapping(trans->address, &mappedRank, &mappedBank, &mappedRow, &mappedCol);

    bool originatedFromLogicOp = trans->originatedFromLogicOp;;
    BusPacket *action, *activate = new BusPacket(ACTIVATE,trans->transactionID,mappedCol,mappedRow,mappedRank,mappedBank,trans->portID,0,trans->mappedChannel,trans->address,trans->originatedFromLogicOp);
    switch(trans->transactionType)
    {
    case DATA_READ:
        readCounter++;
        action = new BusPacket(READ_P,trans->transactionID,mappedCol,mappedRow,mappedRank,mappedBank,trans->portID,trans->transactionSize/DRAM_BUS_WIDTH,trans->mappedChannel,trans->address,trans->originatedFromLogicOp);

#ifdef HMCSIM_SUPPORT
        action->payload = trans->payload;
#endif
        break;
    case DATA_WRITE:
        writeCounter++;
        action = new BusPacket(WRITE_P,trans->transactionID,mappedCol,mappedRow,mappedRank,mappedBank,trans->portID,trans->transactionSize/DRAM_BUS_WIDTH,trans->mappedChannel,trans->address,trans->originatedFromLogicOp);

#ifdef HMCSIM_SUPPORT
        action->payload = trans->payload;
#endif
        delete trans;
        break;
    default:
        ERROR("== ERROR - Adding wrong transaction to simple controller");
        delete activate;
        exit(-1);
    }

    //if requests from logic ops have priority, put them at the front so they go first
    if(GIVE_LOGIC_PRIORITY && originatedFromLogicOp)
    {
      //create column write bus packet and add it to command queue
      this->commandQueue.push_front(action);
      //since we're pushing front, add the ACT after so it ends up being first
      this->commandQueue.push_front(activate);
    }
    else
    {
      //create the row activate bus packet and add it to command queue
      this->commandQueue.push_back(activate);
      //create column write bus packet and add it to command queue
      this->commandQueue.push_back(action);
    }

	waitingACTS++;
}

void SimpleController::AddressMapping(uint64_t physicalAddress, unsigned *rank, unsigned *bank, unsigned *row, unsigned *col)
{
    uint64_t tempA, tempB;

    if(DEBUG_CHANNEL)DEBUGN("     == Mapped 0x"<<hex<<physicalAddress);

    switch(MAPPINGSCHEME)
    {
    case BK_CLH_RW_RK_CH_CLL_BY://bank:col_high:row:rank:chan:col_low:by
        //remove low order bits
        //this includes:
        // - byte offset
        // - low order bits of column address (assumed to be 0 since it is cache aligned)
        // - channel bits (already used)
        //
        //log2(CACHE_LINE_SIZE) == (log2(Low order column bits) + log2(BUS_ALIGNMENT_SIZE))
        physicalAddress >>= (channelBitWidth + cacheOffset);

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= (colBitWidth - (cacheOffset-busOffsetBitWidth));
        tempB = physicalAddress << (colBitWidth - (cacheOffset-busOffsetBitWidth));
        *col = tempA ^ tempB;

        //account for low order column bits
        *col = *col << (cacheOffset-busOffsetBitWidth);

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        break;
    case CLH_RW_RK_BK_CH_CLL_BY://col_high:row:rank:bank:chan:col_low:by
        //remove low order bits
        //this includes:
        // - byte offset
        // - low order bits of column address (assumed to be 0 since it is cache aligned)
        // - channel bits (already used)
        //
        //log2(CACHE_LINE_SIZE) == (log2(Low order column bits) + log2(BUS_ALIGNMENT_SIZE))
        physicalAddress >>= (channelBitWidth + cacheOffset);

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= (colBitWidth - (cacheOffset-busOffsetBitWidth));
        tempB = physicalAddress << (colBitWidth - (cacheOffset-busOffsetBitWidth));
        *col = tempA ^ tempB;

        //account for low order column bits
        *col = *col << (cacheOffset-busOffsetBitWidth);

        break;
    case RK_BK_RW_CLH_CH_CLL_BY://rank:bank:row:colhigh:chan:collow:by
        //remove low order bits
        // - byte offset
        // - low order bits of column address (assumed to be 0 since it is cache aligned)
        // - channel bits (already used)
        //
        //log2(CACHE_LINE_SIZE) == (log2(Low order column bits) + log2(BUS_ALIGNMENT_SIZE))
        physicalAddress >>= (channelBitWidth + cacheOffset);

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= (colBitWidth - (cacheOffset-busOffsetBitWidth));
        tempB = physicalAddress << (colBitWidth - (cacheOffset-busOffsetBitWidth));
        *col = tempA ^ tempB;

        //account for low order column bits
        *col = *col << (cacheOffset-busOffsetBitWidth);

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        break;
    case RW_CH_BK_RK_CL_BY://row:chan:bank:rank:col:by
        //remove low order bits which account for the amount of data received on the bus (8 bytes)
        physicalAddress >>= busOffsetBitWidth;

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= colBitWidth;
        tempB = physicalAddress << colBitWidth;
        *col = tempA ^ tempB;

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        //channel has already been mapped so just shift off the bits
        physicalAddress >>= channelBitWidth;

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        break;
    case RW_BK_RK_CH_CL_BY://row:bank:rank:chan:col:byte
        //remove low order bits which account for the amount of data received on the bus (8 bytes)
        physicalAddress >>= busOffsetBitWidth;

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= colBitWidth;
        tempB = physicalAddress << colBitWidth;
        *col = tempA ^ tempB;

        //channel has already been mapped so just shift off the bits
        physicalAddress >>= channelBitWidth;

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        break;
    case RW_BK_RK_CLH_CH_CLL_BY://row:bank:rank:col_high:chan:col_low:byte
        //remove low order bits
        //this includes:
        // - byte offset
        // - low order bits of column address (assumed to be 0 since it is cache aligned)
        // - channel bits (already used)
        //
        //log2(CACHE_LINE_SIZE) == (log2(Low order column bits) + log2(BUS_ALIGNMENT_SIZE))
        physicalAddress >>= (channelBitWidth + cacheOffset);

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= (colBitWidth - (cacheOffset-busOffsetBitWidth));
        tempB = physicalAddress << (colBitWidth - (cacheOffset-busOffsetBitWidth));
        *col = tempA ^ tempB;

        //account for low order column bits
        *col = *col << (cacheOffset-busOffsetBitWidth);

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        break;
    case RW_CLH_BK_RK_CH_CLL_BY://row:col_high:bank:rank:chan:col_low:byte
        //remove low order bits
        //this includes:
        // - byte offset
        // - low order bits of column address (assumed to be 0 since it is cache aligned)
        // - channel bits (already used)
        //
        //log2(CACHE_LINE_SIZE) == (log2(Low order column bits) + log2(BUS_ALIGNMENT_SIZE))
        physicalAddress >>= (channelBitWidth + cacheOffset);

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= (colBitWidth - (cacheOffset-busOffsetBitWidth));
        tempB = physicalAddress << (colBitWidth - (cacheOffset-busOffsetBitWidth));
        *col = tempA ^ tempB;

        //account for low order column bits
        *col = *col << (cacheOffset-busOffsetBitWidth);

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        break;
    case CH_RW_BK_RK_CL_BY:
        //remove bits which would address the amount of data received on a request
        physicalAddress >>= busOffsetBitWidth;

        //column bits
        tempA = physicalAddress;
        physicalAddress >>= colBitWidth;
        tempB = physicalAddress << colBitWidth;
        *col = tempA ^ tempB;

        //rank bits
        tempA = physicalAddress;
        physicalAddress >>= rankBitWidth;
        tempB = physicalAddress << rankBitWidth;
        *rank = tempA ^ tempB;

        //bank bits
        tempA = physicalAddress;
        physicalAddress >>= bankBitWidth;
        tempB = physicalAddress << bankBitWidth;
        *bank = tempA ^ tempB;

        //row bits
        tempA = physicalAddress;
        physicalAddress >>= rowBitWidth;
        tempB = physicalAddress << rowBitWidth;
        *row = tempA ^ tempB;

        break;
    default:
        ERROR("== ERROR - Unknown address mapping???");
        exit(1);
        break;
    };

    if(DEBUG_CHANNEL) DEBUG(" to RK:"<<hex<<*rank<<" BK:"<<*bank<<" RW:"<<*row<<" CL:"<<*col<<dec);
}
