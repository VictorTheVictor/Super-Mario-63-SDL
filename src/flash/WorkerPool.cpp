#include "flash/WorkerPool.h"

#include <algorithm>

namespace flash {

WorkerPool::WorkerPool(int threads) {
    for (int i = 1; i < std::max(1, threads); i++) workers_.emplace_back([this, i] { work(i); });
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
}

int WorkerPool::defaultThreads() {
    unsigned n = std::thread::hardware_concurrency();
    return (int)std::clamp(n ? n : 1u, 1u, 16u);
}

void WorkerPool::drain(int worker) {
    for (int job; (job = next_.fetch_add(1)) < jobs_;) (*fn_)(job, worker);
}

void WorkerPool::run(int jobs, const std::function<void(int, int)>& fn) {
    if (jobs <= 0) return;
    if (workers_.empty() || jobs == 1) {
        for (int j = 0; j < jobs; j++) fn(j, 0);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn_ = &fn;
        jobs_ = jobs;
        next_ = 0;
        busy_ = (int)workers_.size();
        generation_++;
    }
    wake_.notify_all();
    drain(0);
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return busy_ == 0; });
    fn_ = nullptr;
}

void WorkerPool::work(int worker) {
    unsigned seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return quit_ || generation_ != seen; });
            if (quit_) return;
            seen = generation_;
        }
        drain(worker);
        std::lock_guard<std::mutex> lock(mutex_);
        if (--busy_ == 0) done_.notify_one();
    }
}

}  // namespace flash
