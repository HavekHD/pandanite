#include "request_manager.hpp"
#include "helpers.hpp"
#include "user.hpp"
#include <map>
#include <iostream>
using namespace std;


RequestManager::RequestManager(string hostUrl) {
    this->blockchain = new BlockChain();
    if (hostUrl != "") this->blockchain->sync(hostUrl);
}

json RequestManager::addTransaction(json data) {
    Transaction t = Transaction(data["transaction"]);
    json result;
    this->blockchain->acquire();
    if (t.getBlockId() == this->blockchain->getBlockCount() + 1) { // case 1: add to latest block queue
        ExecutionStatus status = this->blockchain->verifyTransaction(t);
        if (status != SUCCESS) {
            result["error"] = executionStatusAsString(status);
        } else {
            if (this->transactionQueue[t.getBlockId()].size() < MAX_TRANSACTIONS_PER_BLOCK) {
                this->transactionQueue[t.getBlockId()].push_back(t);
                result["message"] = "SUCCESS";
            } else {
                result["error"] = "QUEUE_FULL";
            }
            
        }
    } else if (t.getBlockId() > this->blockchain->getBlockCount() + 1){ // case 2: add to future block queue
        if (t.getBlockId() < this->blockchain->getBlockCount() + 5) {   // limit to queueing up to 5 blocks in future
            if (this->transactionQueue[t.getBlockId()].size() < MAX_TRANSACTIONS_PER_BLOCK) {
                result["message"] = "IN_QUEUE";
                this->transactionQueue[t.getBlockId()].push_back(t);
            } else {
                result["error"] = "QUEUE_FULL";
            }
            
        } else {
            result["error"] = "BLOCK_ID_TOO_LARGE";
        }
    } else {
        result["error"] = "EXPIRED_TRANSACTION";
    }
    this->blockchain->release();
    return result;
}
json RequestManager::submitProofOfWork(json request) {
    json result;
    // build map of all public keys in transaction
    this->blockchain->acquire();
    // parse and add mining fee 
    Block newBlock(request["block"]);

    // add to the chain!
    ExecutionStatus status = this->blockchain->addBlock(newBlock);
    result["status"] = executionStatusAsString(status);
    if (status == SUCCESS) {
        if (this->transactionQueue.find(newBlock.getId()) != this->transactionQueue.end()) {
            this->transactionQueue.erase(newBlock.getId());
        }

        if (this->transactionQueue.find(newBlock.getId()+1) != this->transactionQueue.end()) {
            // validate transactions further down the queue
            list<Transaction>& currTransactions =  this->transactionQueue[newBlock.getId()+1];
            for(auto it = currTransactions.begin(); it != currTransactions.end(); ++it) {
                ExecutionStatus status = this->blockchain->verifyTransaction(*it);
                if ((status) != SUCCESS) {
                    // erase the invalid transaction
                    currTransactions.erase(it);
                }
            }
        }
    }
    this->blockchain->release();
    return result;
}

json RequestManager::getProofOfWork() {
    json result;
    vector<Transaction> transactions;
    // send POW problem + transaction queue item
    result["transactions"] = json::array();
    list<Transaction>& currTransactions = this->transactionQueue[this->blockchain->getBlockCount() + 1];
    for(auto p : currTransactions) result["transactions"].push_back(p.toJson());
    result["lastHash"] = SHA256toString(this->blockchain->getLastHash());
    result["challengeSize"] = this->blockchain->getDifficulty();
    return result;

}

size_t RequestManager::getPendingTransactionSize(int block) {
    if (this->transactionQueue.find(block) == this->transactionQueue.end()) return 0;
    return this->transactionQueue[block].size();
}
json RequestManager::getBlock(int index) {
    return this->blockchain->getBlock(index).toJson();
}
json RequestManager::getLedger(PublicWalletAddress w) {
    json result;
    Ledger& ledger = this->blockchain->getLedger();
    if (!ledger.hasWallet(w)) {
        result["error"] = "Wallet not found";
    } else {
        result["balance"] = ledger.getWalletValue(w);
    }
    return result;
}
string RequestManager::getBlockCount() {
    int count = this->blockchain->getBlockCount();
    return std::to_string(count);
}

json RequestManager::getStats() {
    json info;
    if (this->blockchain->getBlockCount() == 1) {
        info["error"] = "Need more data";
        return info;
    }
    int coins = this->blockchain->getBlockCount()*50;
    int wallets = this->blockchain->getLedger().size();
    info["num_coins"] = coins;
    info["num_wallets"] = wallets;
    info["pending_transactions"]= this->getPendingTransactionSize(this->blockchain->getBlockCount()+2);
    
    int idx = this->blockchain->getBlockCount();
    Block a = this->blockchain->getBlock(idx);
    Block b = this->blockchain->getBlock(idx-1);
    int timeDelta = a.getTimestamp() - b.getTimestamp();
    int totalSent = 0;
    int fees = 0;
    info["transactions"] = json::array();
    for(auto t : a.getTransactions()) {
        totalSent += t.getAmount();
        fees += t.getTransactionFee();
        info["transactions"].push_back(t.toJson());
    }
    int count = a.getTransactions().size();
    info["transactions_per_second"]= a.getTransactions().size()/(double)timeDelta;
    info["transaction_volume"]= totalSent;
    info["avg_transaction_size"]= totalSent/count;
    info["avg_transaction_fee"]= fees/count;
    info["difficulty"]= a.getDifficulty();
    info["current_block"]= this->blockchain->getBlockCount()+1;
    info["last_block_time"]= timeDelta;
    return info;
}