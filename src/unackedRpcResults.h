/* Copyright (c) 2014-2016 Stanford University
 * Copyright (c) 2026 University of Southern California
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef UNACKED_RPC_RESULTS_H
#define UNACKED_RPC_RESULTS_H

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <string>

/**
 * A temporary storage for results of linearizable RPCs that have not been
 * acknowledged by client.
 *
 * Each master should keep an instance of this class to keep the information
 * on which rpc has been processed with its result. The information should be
 * used to avoid processing re-tried RPCs again.
 */
class UnackedRpcResults {
  public:
    explicit UnackedRpcResults();
    ~UnackedRpcResults();
    bool checkDuplicate(std::chrono::system_clock::time_point deadline,
                        uint64_t clientId,
                        uint64_t rpcId,
                        uint64_t ackId,
                        std::string** respPtrOut);
    std::string* recordCompletion(uint64_t clientId,
                          uint64_t rpcId,
                          std::string& response,
                          bool ignoreIfAcked = false);
    void recoverRecord(uint64_t clientId,
                        uint64_t rpcId,
                        uint64_t ackId,
                        std::string& response);
    void resetRecord(uint64_t clientId,
                     uint64_t rpcId);
    bool isRpcAcked(uint64_t clientId, uint64_t rpcId);

  private:
    void cleanByTimeout();
    /// Used only for testing.
    bool hasRecord(uint64_t clientId, uint64_t rpcId);

    /**
     * Holds info about outstanding RPCs, which is needed to avoid re-doing
     * the same RPC.
     */
    struct UnackedRpc {
        UnackedRpc(uint64_t rpcId = 0, std::string* resp = nullptr)
            : id(rpcId), response() {
            if (resp != nullptr) {
                response = *resp;
            }
        }

        /**
         * Rpc id assigned by a client.
         */
        uint64_t id;

        /**
         * Serialized RPC response.
         */
        std::string response;
    };

    /**
     * Each instance of this class stores information about unacknowledged RPCs
     * from a single client, which is determined by the client id.
     */
    class Client {
      public:
        /**
         * Constructor for Client
         *
         * \param size
         *      Initial size of the array which keeps UnackedRpc.
         */
        explicit Client(int size)
            : maxRpcId(0)
            , maxAckId(0)
            , numRpcsInProgress(0)
            , rpcs(new UnackedRpc[size]())
            , len(size)
            , lastActivityTime(std::chrono::system_clock::now())
        {}

        ~Client() {
            delete[] rpcs;
        }

        bool hasRecord(uint64_t rpcId);
        std::string* result(uint64_t rpcId);
        void recordNewRpc(uint64_t rpcId);
        void updateResult(uint64_t rpcId, std::string& result);
        void processAck(uint64_t ackId);
        void resizeRpcs(int newLen);

        /**
         * Keeps the largest RpcId from the client seen in this master.
         * This is used to optimize checkDuplicate() in general case.
         */
        uint64_t maxRpcId;

        /**
         * Largest RpcId for which the client has acknowledged receiving
         * the result. We do not need to retain information for
         * RPCs with rpcId <= maxAckId.
        */
        uint64_t maxAckId;

        /**
         * The count for rpcIds in stage between checkDuplicate and
         * recordCompletion (aka. in Progress).
         * The count is used while cleanup to prevent removing client
         * with rpcs in progress.
         */
        int numRpcsInProgress;

        /**
         * Dynamically allocated array keeping #UnackedRpc of this client.
         * This keeps the status of each RPC until being acknowledged.
         * The array is initially with small size, let's say 5. Then, the info
         * about a rpc with id = i is recorded in (i % 5)th index of the array.
         *
         *   Example of array #Client::rpcs
         * | index  |  0  |  1  |  2  |  3  |  4  |
         * | rpc id |  5  | 11  |  7  |  8  |  9  |
         *
         * If the index (i % len), where #Client::len is the size of array, is
         * already occupied with other rpc which is not yet acknowledged, we
         * need to increase the size of array. But the maximum size of array
         * is bounded by the hard limit of the maximum number of outstanding
         * RPC by a client.
         */
        UnackedRpc* rpcs;

        /**
         * Length of the dynamic array, #rpcs.
         */
        int len;

        /**
         * The time point when the last activity from the client happened.
         * (e.g., the latest RPC deadline.)
         * This is used to clean up stale client records.
         */
        std::chrono::system_clock::time_point lastActivityTime;
    };

    /**
     * Maps from a registered client ID to #Client.
     * Clients are dynamically allocated and must be freed explicitly.
     */
    typedef std::unordered_map<uint64_t, Client*> ClientMap;
    ClientMap clients;

    /**
     * Monitor-style lock. Any operation on internal data structure should
     * hold this lock.
     */
    std::mutex mutex;
    typedef std::lock_guard<std::mutex> Lock;

    /**
     * This value is used as initial array size of each Client instance.
     */
    int default_rpclist_size;

    // Helper methods
    Client* getClientRecord(uint64_t clientId, Lock& lock);
    Client* getOrInitClientRecord(uint64_t clientId, Lock& lock);
};

/**
 * A linearizable RPC handler should construct this class to check duplicate
 * RPC in progress and to record the result of RPC after processing.
 *
 * This handle should be used instead of directly calling UnackedRpcResults
 * for exception safety.
 *
 * Destruction of this class should happen only after log-sync.
 */
class UnackedRpcHandle {
  public:
    UnackedRpcHandle(UnackedRpcResults* unackedRpcResults,
                     std::chrono::system_clock::time_point deadline,
                     uint64_t clientId,
                     uint64_t rpcId,
                     uint64_t ackId);
    UnackedRpcHandle(const UnackedRpcHandle& origin);
    UnackedRpcHandle& operator= (const UnackedRpcHandle& origin);
    ~UnackedRpcHandle();

    bool isDuplicate();
    bool isInProgress();
    std::string& savedResponse();
    void recordCompletion(std::string& response);

  private:
    /// Save clientId and rpcId to be used for recordCompletion later.
    /// We do this double lookup to circumvent problem while resizing rpcs array
    uint64_t clientId;
    uint64_t rpcId;

    /// Keeps the outcome of checkDuplicate() call in constructor.
    bool duplicate;

    /// Reference to the saved RPC response.
    /// If it is a duplicate RPC, obtained from checkDuplicate() in constructor.
    /// If it is a new RPC, saved by #UnackedRpcHandle::recordCompletion() call.
    std::string* respPtr;

    /// Pointer to current unackedRpcResults.
    UnackedRpcResults* rpcResults;
};

#endif // UNACKED_RPC_RESULTS_H
