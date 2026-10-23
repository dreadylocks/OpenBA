// ============================================================================
// src/ext/path_workers/pool.cpp -- ext.path_workers: the worker pool core
// ============================================================================
// NOT RETAIL. pool.h has the model and the contract.
// ============================================================================
#include "ext/path_workers/pool.h"

namespace Ext {
namespace PathWorkers {

Pool::Pool(Runner& runner, int workers)
    : runner_(runner), workers_(workers < 1 ? 1 : (workers > kMaxWorkers ? kMaxWorkers : workers)) {}

void Pool::Submit(const Job& job) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (stop_) return;
        queue_.push_back(job);
        ++inFlight_;
    }
    work_.notify_one();
}

void Pool::Barrier() {
    std::unique_lock<std::mutex> lk(mutex_);
    idle_.wait(lk, [this] { return inFlight_ == 0; });
}

int Pool::InFlight() {
    std::lock_guard<std::mutex> lk(mutex_);
    return inFlight_;
}

void Pool::WorkerMain(int worker) {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            work_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) return;   // stopped and drained
            job = queue_.front();
            queue_.pop_front();
        }
        runner_.RunJob(worker, job);
        bool idle;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            ++completed_;
            idle = --inFlight_ == 0;
        }
        if (idle) idle_.notify_all();
    }
}

void Pool::Stop() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        stop_ = true;
    }
    work_.notify_all();
}

long long Pool::Completed() {
    std::lock_guard<std::mutex> lk(mutex_);
    return completed_;
}

}  // namespace PathWorkers
}  // namespace Ext
