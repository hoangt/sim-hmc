#include <cassert>
#include "hmc_queue.h"

hmc_queue::hmc_queue(void) :
  id( -1 ),
  notify( NULL ),
  bitoccupation( 0 ),
  bitoccupationmax( 0 ),
  bitwidth( 0 )
{
}

hmc_queue::~hmc_queue(void)
{
}

void hmc_queue::set_notify(unsigned id, hmc_notify *notify)
{
  this->id = id;
  this->notify = notify;
}

void hmc_queue::re_adjust(unsigned bitwidth, unsigned queuedepth)
{
  this->bitwidth = bitwidth;
  this->bitoccupationmax = bitwidth * queuedepth;
}

bool hmc_queue::has_space(unsigned packetleninbit)
{
  return (this->bitoccupation /* + packetleninbit */ < this->bitoccupationmax);
}

int hmc_queue::push_back(void *packet, unsigned packetleninbit)
{
  if( this->bitoccupation /* + packetleninbit */ < this->bitoccupationmax ) {
    this->bitoccupation += packetleninbit;
    unsigned cycles = packetleninbit / this->bitwidth;
    if( ! cycles )
      cycles = 1;
    this->list.push_back(std::make_tuple(packet, cycles, packetleninbit));
    if( this->notify != NULL )
      this->notify->notify_add(this->id);
    return 0;
  }
  return -1;
}

void* hmc_queue::front(unsigned *packetleninbit)
{
  assert( ! this->list.empty());
  std::list< std::tuple<void*, unsigned, unsigned> > * q = &this->list;
  // ToDo: shall be all decreased or just the first ones?
  for(auto it=q->begin(); it != q->end(); ++it)
  {
    unsigned cyclestowait = std::get<1>(*it);
    if(cyclestowait > 0)
      cyclestowait = --std::get<1>(q->front());
  }
  unsigned cyclestowait = std::get<1>(q->front());
  *packetleninbit = std::get<2>(q->front());
  return ( ! cyclestowait) ? std::get<0>(q->front()) : nullptr;
}

void* hmc_queue::pop_front(void)
{
  std::list< std::tuple<void*, unsigned, unsigned> > * q = &this->list;
  void* front = std::get<0>(q->front());
  this->bitoccupation -= std::get<2>(q->front());
  q->pop_front();
  if( this->notify != NULL )
    this->notify->notify_del(this->id);
  return front;
}
