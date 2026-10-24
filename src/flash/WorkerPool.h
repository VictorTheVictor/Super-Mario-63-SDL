#pragma once
// A fixed set of worker threads for splitting one job (such as rendering a frame) into
// independent pieces. Uses only the C++ standard library.

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace flash {

class WorkerPool {
public:
    // `threads` counts the calling thread, which also works; 1 runs everything inline.
    explicit WorkerPool(int threads);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    int size() const { return (int)workers_.size() + 1; }

    // Calls fn(job, worker) for every job in [0, jobs), spread over the threads, and
    // returns when all have finished. `worker` is in [0, size()); 0 is the caller.
    void run(int jobs, const std::function<void(int job, int worker)>& fn);

    // A sensible default: the machine's hardware threads, capped.
    static int defaultThreads();

private:
    void work(int worker);
    void drain(int worker);

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(int, int)>* fn_ = nullptr;
    int jobs_ = 0;
    std::atomic<int> next_{0};
    int busy_ = 0;            // workers still in the current batch
    unsigned generation_ = 0;  // bumped for every batch
    bool quit_ = false;
};

}  // namespace flash
