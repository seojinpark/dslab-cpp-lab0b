#include <iostream>
#include <memory>
#include <string>
#include <cstring>
#include <getopt.h>
#include "rpcClient.h"
#include "ddb/integration.hpp"

// Logging and tracing includes
#include "common/logger.hpp"
#include "spdlog/spdlog.h"
#ifdef TRACING
#include "common/utils/tracing.hpp"
#endif

char* ipAndPort;           // includes port number.
char* inputText;
bool resetCounter = false;
bool shutdownServer = false;

// DDB: options
bool ddb = false;
char* ddb_host_ip = (char*)"127.0.0.1";
char* ddb_proc_alias = (char*)"wc_client";

// Logger instance
std::unique_ptr<accumulator::utils::logger> logger;

void parse_args(int argc, char** argv) {
  static struct option long_options[] = {
      {"dest", required_argument, NULL, 'i'},
      {"reset", no_argument, NULL, 'r'},
      {"shutdown", no_argument, NULL, 's'},
      {"text", required_argument, NULL, 't'},
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
        ipAndPort = optarg;
        break;
      case 'r':
        resetCounter = true;
        break;
      case 's':
        shutdownServer = true;
        break;
      case 't':
        inputText = optarg;
        break;
      default:
        printf("?? getopt returned character code 0%o ??\n", ch);
    }
  }
}

int main(int argc, char** argv) {
  parse_args(argc, argv);
  // DDB: initialization
  if (ddb) {
    auto cfg = DDB::Config::get_default(ddb_host_ip)
                   .with_alias(ddb_proc_alias)
                   .with_hash(ddb_proc_alias);
    auto connector = DDB::DDBConnector(cfg);
    connector.init();
  }
  
  // Initialize logger infrastructure
  accumulator::utils::init_logger();
  
  std::string service_name = "wc_client";

#ifdef TRACING
  // Initialize OpenTelemetry tracing and logging
  tracing::InitOtelInfra(service_name);
#endif

  // Create logger instance for this service
  logger = accumulator::utils::logger::get_logger(service_name);

#ifdef TRACING
  grpc::ChannelArguments args;
  auto channel = grpc::experimental::CreateCustomChannelWithInterceptors(
      ipAndPort,
      grpc::InsecureChannelCredentials(),
      args,
      tracing::CreateClientTracingInterceptors());
#else
  auto channel = grpc::CreateChannel(ipAndPort, grpc::InsecureChannelCredentials());
#endif

  RpcClient client(channel);

  if (resetCounter) {
    client.ResetCounter();
    std::cout << "Reset invoked." << std::endl;
  } else if (shutdownServer) {
    client.Shutdown();
    std::cout << "Shutdown requested." << std::endl;
  } else {
    auto [wc, wcSum] = client.AddWordCount(inputText);
    std::cout << "Word count: " << wc << std::endl
              << "Sum of all word counts: " << wcSum << std::endl;
    
    // Example: logger usage
    logger->info("Example log: without tracing, this outputs to terminal & file sinks. "
      "With tracing, this goes to Alloy (Grafana's telemetry collector). "
      "If the collector is Jaeger, which doesn't support logging, logs will be dropped.");
    
    logger->debug("Example debug log. Set the SPDLOG_LEVEL to show.");
  }

  logger.reset();
  spdlog::shutdown();

  return 0;
}
