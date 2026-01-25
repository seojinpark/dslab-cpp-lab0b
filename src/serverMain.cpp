#include <atomic>
#include <iostream>
#include <thread>
#include <memory>
#include <string>
#include <cstring>
#include <getopt.h>
#include "rpcService.h"
#include "ddb/integration.hpp"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include "accumulator.grpc.pb.h"

// Logging and tracing includes
#include "common/logger.hpp"
#ifdef TRACING
#include "common/utils/tracing.hpp"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

char* myAddr;           // includes port number.
std::unique_ptr<AccumulatorServiceImpl> grpcService;
std::unique_ptr<grpc::Server> grpcServer;

// DDB: options
bool ddb = false;
char* ddb_host_ip = (char*)"127.0.0.1";
char* ddb_proc_alias = (char*)"wc_server";

// Logger instance
std::unique_ptr<accumulator::utils::logger> logger;

void initGrpcServer() {
  std::string server_address(myAddr);
  grpcService = std::make_unique<AccumulatorServiceImpl>();
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(grpcService.get());
  
#ifdef TRACING
  // Add tracing interceptors to the server
  builder.experimental().SetInterceptorCreators(
      tracing::CreateServerTracingInterceptors());
#endif

  grpcServer = builder.BuildAndStart();
  grpcService->setGrpcServer(grpcServer.get());
  
  logger->info("Server listening on {}", server_address);
  std::cout << "Server listening on " << server_address << std::endl;
}

void parse_args(int argc, char** argv) {
  static struct option long_options[] = {
      {"bind", required_argument, NULL, 'i'},
      // DDB: options
      {"ddb", no_argument, NULL, 0},
      {"ddb_host_ip", required_argument, NULL, 0},
      {"ddb_proc_alias", required_argument, NULL, 0},
      {NULL, 0, NULL, 0}
  };

  // loop over all of the options
  signed char ch;
  int option_index = 0;
  while ((ch = getopt_long(argc, argv, "t:a:", long_options, &option_index)) != -1) {
    switch (ch) {
      case 0:
        // DDB: options
        // Long-only options
        if (strcmp(long_options[option_index].name, "ddb") == 0) {
          ddb = true;
        } else if (strcmp(long_options[option_index].name, "ddb_host_ip") == 0) {
          ddb_host_ip = optarg;
        } else if (strcmp(long_options[option_index].name, "ddb_proc_alias") == 0) {
          ddb_proc_alias = optarg;
        }
        break;
      case 'i':
        myAddr = optarg;
        break;

      default:
        printf("?? getopt returned character code 0%o ??\n", ch);
    }
  }
}

int main(int argc, char** argv) {
  parse_args(argc, argv);
  if (ddb) {
    auto cfg = DDB::Config::get_default(ddb_host_ip)
                   .with_alias(ddb_proc_alias)
                   .with_hash(ddb_proc_alias);
    auto connector = DDB::DDBConnector(cfg);
    connector.init();
  }
  
  // Initialize logger infrastructure
  accumulator::utils::init_logger();
  
  std::string service_name = "wc_server";
  
#ifdef TRACING
  // Initialize OpenTelemetry tracing and logging
  tracing::InitOtelInfra(service_name);
#endif

  // Create logger instance for this service
  logger = accumulator::utils::logger::get_logger(service_name);
  logger->info("Starting {} service", service_name);

  initGrpcServer();
  grpcServer->Wait();
  
  shutdown_thread->join();
  return 0;
}
