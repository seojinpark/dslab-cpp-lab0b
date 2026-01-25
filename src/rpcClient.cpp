#include "rpcClient.h"
#include <grpcpp/client_context.h>
#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>
#include "accumulator.grpc.pb.h"
#include "accumulator.pb.h"

RpcClient::RpcClient(std::shared_ptr<grpc::ChannelInterface> channel)
  : stub_(Accumulator::NewStub(channel))
  , rpcTracker()
  , clientId(0) {
  
  // TODO: Milestone 2
  // Generates a random clientId.
  // You may change the following code to get more creative.
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis; 
  clientId = dis(gen);
}

std::pair<int, int>
RpcClient::AddWordCount(std::string text) {
  AddWordCountRequest request;
  request.set_text(text);
  grpc::ClientContext context;
  AddWordCountReply reply;
  Status status = stub_->AddWordCount(&context, request, &reply);
  if (status.ok()) {
    return {reply.word_count(), reply.cummulative_count()};
  } else {
    std::cerr << "Failed to add word count. code: " << status.error_code()
              << " msg: " << status.error_message().c_str();
    return {-1, -1};
  }
}

int
RpcClient::GetAllWordCount() {
  Empty request;
  GetAllWordCountReply reply;
  grpc::ClientContext context;
  Status status = stub_->GetAllWordCount(&context, request, &reply);
  if (status.ok()) {
    return reply.cummulative_count();
  } else {
    std::cerr << "Failed to get all word count. code: " << status.error_code()
              << " msg: " << status.error_message().c_str();
    return -1;
  }
}

std::string
RpcClient::ResetCounter() {
  Empty request;
  request.set_clientid(clientId);
  request.set_rpcid(rpcTracker.newRpcId());
  request.set_ackid(rpcTracker.ackId());
  StandardReply reply;
  Status status;
  auto timeout = std::chrono::milliseconds(500);
  for (int attempt = 0; attempt < 4; ++attempt) {
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + timeout);
    status = stub_->ResetCounter(&context, request, &reply);
    if (status.ok()) {
      // TODO (Milestone 3): record rpcFinished to rpcTracker.
      return reply.message();
    } else {
      std::cerr << "Attempt " << attempt << " to reset counter failed. code: "
          << status.error_code() << " msg: " << status.error_message().c_str()
          << " timeout: " << timeout.count() << "ms" << std::endl;
      // Add a spdlog message with warning level with timeout value.
    }
    // TODO (Milestone 2): implement retry with exponential backoff.
  }
  std::cerr << "Failed to reset counter. code: " << status.error_code()
            << " msg: " << status.error_message().c_str();
  return "Failed to reset counter.";
}

std::string
RpcClient::Shutdown() {
  Empty request;
  grpc::ClientContext context;
  StandardReply reply;
  Status status = stub_->Shutdown(&context, request, &reply);
  if (status.ok()) {
    return reply.message();
  } else {
    std::cerr << "Failed to request shutdown. code: "<< status.error_code()
              <<  " msg: %s." << status.error_message().c_str();
    return "Failed to request shutdown.";
  }
}
