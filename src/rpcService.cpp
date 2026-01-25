#include "rpcService.h"
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "accumulator.grpc.pb.h"
#include "accumulator.pb.h"

#define UNUSED(expr) (void)(expr)

using grpc::Server;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

std::unique_ptr<std::thread> shutdown_thread;

void
AccumulatorServiceImpl::setGrpcServer(grpc::Server* serverPtr) {
  grpcServerPtr = serverPtr;
}


/**
 * Helper function for counting words in a string.
 * Use it for implementing AccumulatorServiceImpl::AddWordCount.
 */
int
AccumulatorServiceImpl::countWords(const std::string& text) {
  std::stringstream stream(text);
  std::string oneWord;
  int wc = 0;
  while (stream >> oneWord) {
    ++wc;
  }
  return wc;
}

//////////////////////////////////////////////////////////////////
// RPC service implementations below
//////////////////////////////////////////////////////////////////

Status
AccumulatorServiceImpl::AddWordCount(ServerContext* context,
    const AddWordCountRequest* request,
    AddWordCountReply* reply) {
  UNUSED(context);

// Example of manually starting a span
#ifdef TRACING
  // Example: logs outside the span can only be viewed in Grafana Loki
  this->logger->info("Example log outside to the span.");
  {
  auto tracer = tracing::detail::Tracer();
  auto span = tracer->StartSpan("AddWordCountManualSpan", {});
  auto scope = tracer->WithActiveSpan(span);
  // Example: logs inside the span will be associated with that span in Grafana Tempo
  this->logger->info("Example log attached to the span.");
#endif

  int newly_added = countWords(request->text());
  this->wcSum += newly_added;
  reply->set_word_count(newly_added);
  reply->set_cummulative_count(this->wcSum);

#ifdef TRACING
  }
  // Example: when the span fall out of scope here,
  // logs will not be associated with that span
  this->logger->info("Span fall out of scope: this will not be associated.");
#endif
  
  return Status::OK;
}

Status
AccumulatorServiceImpl::GetAllWordCount(ServerContext* context,
    const Empty* request,
    GetAllWordCountReply* reply) {
  UNUSED(context);
  UNUSED(request);

  // TODO (Milestone1): implement
  reply->set_cummulative_count(this->wcSum);
  return Status::OK;
}

Status
AccumulatorServiceImpl::ResetCounter(ServerContext* context,
    const Empty* request,
    StandardReply* reply) {
  UNUSED(context);
  UNUSED(request);

  uint64_t clientId = request->clientid();
  uint64_t rpcId = request->rpcid();
  uint64_t ackId = request->ackid();
  UnackedRpcHandle rh(&unackedRpcResults, context->deadline(),
      clientId, rpcId, ackId);
  if (rh.isDuplicate()) {
    // Duplicate RPC, return the saved response.
    // TODO (Milestone 3): update the following line to parse the serialized reply.
    reply->set_message(rh.savedResponse());
    return Status::OK;
  }

  std::string replyMsg("Reset Counter invoked.");
  reply->set_message(replyMsg);
  this->wcSum = 0;

  // TODO: remove this sleep after Milestone 3.
  // Sleep for 5 seconds to simulate a long processing time.
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // TODO (Milestone 3): update the following line to save the serialized reply.
  rh.recordCompletion(replyMsg);
  return Status::OK;
}

Status
AccumulatorServiceImpl::Shutdown(ServerContext* context, const Empty* request,
    StandardReply* reply) {
  UNUSED(context);
  UNUSED(request);

  std::string replyMsg("Shutdown invoked.");
  reply->set_message(replyMsg);

  // Shutdown() must be called from another thread. So, we create a new thread.
  // Don't worry about this complex threading for other RPCs.
  auto shutdownFn = [&](Server* serverPtr) {
    std::cout << "grpcServer shutdown in 3 sec." << std::endl << std::flush;
    std::this_thread::sleep_for (std::chrono::seconds(3));
    serverPtr->Shutdown(); //Wait();
    std::cout << "grpcServer shutdown." << std::endl << std::flush;
  };
  shutdown_thread = std::make_unique<std::thread>(shutdownFn, grpcServerPtr);

  return Status::OK;
}
