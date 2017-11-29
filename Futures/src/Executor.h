#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <iostream>

class DrivenExecutor {
    public:
        // Add a task to the executor's work queue
        void execute(std::function<void()> task) {
            std::unique_lock<std::mutex> lock(queueLock_);
            tasks_.push(task);
            
            cv_.notify_all();
        }

        // Run, blocking the calling thread until terminate is called
        void run() {
            while(terminateAfter_ != 0) {
                std::function<void()> nextFunction;
                {
                    std::unique_lock<std::mutex> lock(queueLock_);
                    if(tasks_.empty()) {
                        cv_.wait(lock);
                    }
                    if(!tasks_.empty()) {
                        nextFunction = tasks_.front();
                        tasks_.pop();
                    }
                    if(terminateAfter_ > 0) {
                        --terminateAfter_;
                    }
                }
                if(nextFunction) {
                    nextFunction();
                }
            }
        }

        // Terminate soon. Will drain events already in the queue before
        // terminate is called.
        void terminate() {
            std::unique_lock<std::mutex> lock(queueLock_);
            terminateAfter_ = tasks_.size();
            cv_.notify_all();
        }

    private:
        std::queue<std::function<void()>> tasks_;
        std::atomic<int> terminateAfter_{-1};
        std::mutex queueLock_;
        std::condition_variable cv_;
};
