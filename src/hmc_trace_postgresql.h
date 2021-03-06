#ifndef _HMC_TRACE_POSTGRESQL_H_
#define _HMC_TRACE_POSTGRESQL_H_

#include <cstdint>
#include <pqxx/pqxx>
#include "hmc_trace.h"

class hmc_postgresql : public hmc_trace_logger {
private:
  pqxx::connection* db_conn;
  pqxx::work* trans;

public:
  explicit hmc_postgresql(const char* dbname = "hmcsim", const char* dbuser = "hmcsim", const char* dbpasswd = "hmcsim",
                          const char* dbaddr = "127.0.0.1", const char* dbport = "5432");
  ~hmc_postgresql(void);

  void execute(enum hmc_link_type linkTypeId, unsigned linkIntTypeId,
               uint64_t cycle, uint64_t phyPktAddr,
               int fromCubId, int toCubId,
               int fromId, int toId,
               uint64_t header, uint64_t tail);
};

#endif /* #ifndef _HMC_TRACE_POSTGRESQL_H_ */
