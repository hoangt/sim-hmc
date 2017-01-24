#include <iostream>
#include <cstring>
#include <sys/time.h>
#include "src/hmc_sim.h"

int main(int argc, char* argv[])
{
  unsigned cubes = 2;
  unsigned capacity = 4;
  hmc_sim sim(cubes, 4, 4, capacity, HMCSIM_FULL_LINK_WIDTH, HMCSIM_BR30);
  unsigned slidId = 3;
  unsigned destcub = 0; // 1

#if 1
  // if GRAPHVIZ is enabled!
  hmc_notify* slidnotify = sim.hmc_get_slid_notify(slidId);
  if(slidnotify == nullptr) {
    std::cerr << "initialisation failed!" << std::endl;
    exit(-1);
  }
#else
  hmc_notify* slidnotify = sim.hmc_define_slid(slidId, 0, 0, HMCSIM_FULL_LINK_WIDTH, HMCSIM_BR30);

  bool ret = sim.hmc_set_link_config(0, 1, 1, 0, HMCSIM_FULL_LINK_WIDTH, HMCSIM_BR30);
  ret &= sim.hmc_set_link_config(0, 3, 1, 2, HMCSIM_FULL_LINK_WIDTH, HMCSIM_BR30);
  if (!ret || slidnotify == nullptr) {
    std::cerr << "link setup was not successful" << std::endl;
  }
#endif

  unsigned sendpacketleninbit = 2*FLIT_WIDTH;
  char packet[(17*FLIT_WIDTH) / 8];

  unsigned issue_writes = 0;
  unsigned issue_reads = 20; //60000;

  unsigned issue_sum = issue_writes + issue_reads;
  unsigned send_ctr = 0;
  unsigned skip = 0;
  unsigned recv_ctr = 0 + skip;

  unsigned clks = 0;
  unsigned *track = new unsigned[issue_sum];

  struct timeval t1, t2;
  gettimeofday(&t1, NULL);

  char retpacket[17*FLIT_WIDTH / (sizeof(char)*8)];
  bool next_available = false;
  do
  {
    if(issue_sum > send_ctr && next_available == false)
    {
      memset(packet, 0, (sendpacketleninbit / FLIT_WIDTH << 1) * sizeof(uint64_t));

      unsigned addr;
      switch(send_ctr & 0x3) {
      default:
      case 0x0:
        addr = 0b000000000000; // quad 0
        break;
      case 0x1:
        addr = 0b010000000000; // quad 1
        break;
      case 0x2:
        addr = 0b100000000000; // quad 2
        break;
      case 0x3:
        addr = 0b110000000000; // quad 3
        break;
      }
      if(send_ctr < issue_reads) {
        unsigned dram_hi = (send_ctr & 0b111) << 4;
        unsigned dram_lo = (send_ctr >> 3) << (capacity == 8 ? 16 : 15);
        sim.hmc_encode_pkt(destcub, addr+dram_hi+dram_lo, send_ctr /* tag */, RD256, packet);
      }
      else
        sim.hmc_encode_pkt(destcub, addr, send_ctr /* tag */, WR64, packet);
      next_available = true;
    }
    if(next_available == true && sim.hmc_send_pkt(slidId, packet)) {
      track[send_ctr] = clks;
      send_ctr++;
//      if(!(send_ctr % 100))
//        std::cout << "issued " << send_ctr << std::endl;
      next_available = false;
    }

    if(slidnotify->get_notification() && sim.hmc_recv_pkt(slidId, retpacket))
    {
      track[recv_ctr] = clks - track[recv_ctr];
      recv_ctr++;
//      if(!(recv_ctr % 100))
//        std::cout << "received " << recv_ctr << std::endl;
      if(recv_ctr >= issue_sum)
        break;
    }
    // set clk anyway
    clks++;
    //if(clks > 311)
    //  exit(0);
    sim.clock();
  } while(true);

  gettimeofday(&t2, NULL);

  unsigned long long avg = 0;
  for(unsigned i=0; i<issue_sum-skip; i++) {
    avg += track[i];
  }
  avg /= (issue_sum-skip);

  delete[] track;

  // ToDo: account not only for reads but also writes! and depending on the type send!
  std::cout << "issued: " << issue_sum << " (skip: " << skip << ")" << std::endl;
  float freq = 0.8f; // we clk at the same frequency! otherwise: 0.8f
  std::cout << "done in " << clks << " clks, avg.: " << avg << std::endl;
  float bw = (((float)(256+16)*8*(issue_sum-skip))/(clks*freq)); // Gbit/s
  std::cout << "bw: " << bw << "Gbit/s, " << (bw/8) << "GB/s"  << std::endl;
  std::cout << "bw per lane: " << (((float)(256+16)*8*(issue_sum-skip))/(clks*freq*16)) << "Gbit/s" << std::endl;

  double elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0; // ms
  std::cout << std::endl;
  std::cout << "Simulation time: " << elapsedTime << " ms, cycles: " << clks << ", " << (clks/elapsedTime) << " kHz" << std::endl;

  return 0;
}
