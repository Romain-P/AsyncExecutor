#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>

template <typename ResultType>
struct Task {
    uint32_t id;
    std::optional<ResultType> result;
    std::exception_ptr exception;
};

class AsyncExecutor {
public:
    AsyncExecutor() : task_id_counter(0), finished_task_count(0), max_valid_task_id(0) {}

    ~AsyncExecutor() {
        cancel_all();
    }

    template <typename ResultType>
    uint32_t execute(std::function<ResultType()> task) {
        uint32_t task_id = task_id_counter++;
        std::thread([this, task_id, task]() {
            try {
                auto result = std::make_shared<ResultType>(task());
                mark_task_finished<ResultType>(task_id, result);
            } catch (...) {
                mark_task_exception<ResultType>(task_id, std::current_exception());
            }
        }).detach();
        return task_id;
    }

    template <typename ResultType>
    std::shared_ptr<Task<ResultType>> try_pop() {
        if (finished_task_count.load(std::memory_order_relaxed) == 0) {
            return nullptr;
        }

        if (!mutex_.try_lock()) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(mutex_, std::adopt_lock);

        if (!finished_tasks.empty()) {
            auto it = finished_tasks.begin();
            auto base_task = std::move(it->second);
            finished_tasks.erase(it);
            finished_task_count.fetch_sub(1, std::memory_order_relaxed);

            auto task = std::static_pointer_cast<Task<ResultType>>(base_task);
            return task;
        }

        return nullptr;
    }

    void cancel(uint32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_task_ids.insert(id);
    }

    void cancel_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        max_valid_task_id.store(task_id_counter.load(std::memory_order_relaxed), std::memory_order_relaxed);
        cancelled_task_ids.clear();
    }

private:
    template <typename ResultType>
    void mark_task_finished(uint32_t task_id, std::shared_ptr<ResultType> result) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (task_id < max_valid_task_id.load(std::memory_order_relaxed) || cancelled_task_ids.count(task_id)) {
            return;
        }
        auto task = std::make_shared<Task<ResultType>>();
        task->id = task_id;
        task->result = *result;
        finished_tasks.emplace(task_id, task);
        finished_task_count.fetch_add(1, std::memory_order_relaxed);
    }

    template <typename ResultType>
    void mark_task_exception(uint32_t task_id, std::exception_ptr eptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (task_id < max_valid_task_id.load(std::memory_order_relaxed) || cancelled_task_ids.count(task_id)) {
            return;
        }
        auto task = std::make_shared<Task<ResultType>>();
        task->id = task_id;
        task->exception = eptr;
        finished_tasks.emplace(task_id, task);
        finished_task_count.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<uint32_t> task_id_counter;
    std::atomic<int> finished_task_count;
    std::atomic<uint32_t> max_valid_task_id;
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<void>> finished_tasks;
    std::unordered_set<uint32_t> cancelled_task_ids;
};
