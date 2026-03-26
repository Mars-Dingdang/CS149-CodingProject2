#include "tasksys.h"
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>


IRunnable::~IRunnable() {}

ITaskSystem::ITaskSystem(int num_threads) {}
ITaskSystem::~ITaskSystem() {}

/*
 * ================================================================
 * Serial task system implementation
 * ================================================================
 */

const char* TaskSystemSerial::name() {
    return "Serial";
}

TaskSystemSerial::TaskSystemSerial(int num_threads): ITaskSystem(num_threads) {
}

TaskSystemSerial::~TaskSystemSerial() {}

void TaskSystemSerial::run(IRunnable* runnable, int num_total_tasks) {
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                          const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemSerial::sync() {
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelSpawn::name() {
    return "Parallel + Always Spawn";
}

TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    this->num_threads = num_threads;

}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    std::thread workers[num_threads];
    auto worker = [&](int thread_id) {
        for (int i = thread_id; i < num_total_tasks; i += num_threads) {
            runnable->runTask(i, num_total_tasks);
        }
    };
    for (int i = 1; i < num_threads; i++) {
        workers[i] = std::thread(worker, i);
    }
    worker(0);
    for (int i = 1; i < num_threads; i++) {
        workers[i].join();
    }

    // for (int i = 0; i < num_total_tasks; i++) {
    //     runnable->runTask(i, num_total_tasks);
    // }
}

TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                 const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelSpawn::sync() {
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Spinning Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelThreadPoolSpinning::name() {
    return "Parallel + Thread Pool + Spin";
}

TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads): ITaskSystem(num_threads), num_threads(num_threads), next_task_id(0), is_killed(false), tasks_done(0), current_runnable(nullptr) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //

    this->num_total_tasks = 0;

    workers.reserve(num_threads);
    for(int i = 0; i < num_threads; i++) {
        workers.emplace_back(&TaskSystemParallelThreadPoolSpinning::workerLoop, this);
    }

}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    is_killed = true;
    for(std::thread &worker : workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

void TaskSystemParallelThreadPoolSpinning::workerLoop() {
    while (!is_killed) {
        int task_id = next_task_id.fetch_add(1);
        if (current_runnable != nullptr && task_id < num_total_tasks) {
            current_runnable->runTask(task_id, num_total_tasks);
            tasks_done.fetch_add(1);
        }
        else {
            std::this_thread::yield();
        }
    }
}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {

    current_runnable = runnable;
    this->num_total_tasks = num_total_tasks;    
    next_task_id = 0;
    tasks_done = 0;

    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    for(; tasks_done.load() < num_total_tasks; ) {
        std::this_thread::yield();
    }
    this->num_total_tasks = 0;
    current_runnable = nullptr;

    // for (int i = 0; i < num_total_tasks; i++) {
    //     runnable->runTask(i, num_total_tasks);
    // }
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelThreadPoolSpinning::sync() {
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Sleeping Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelThreadPoolSleeping::name() {
    return "Parallel + Thread Pool + Sleep";
}

TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads): ITaskSystem(num_threads), num_threads(num_threads), is_killed(false), current_runnable(nullptr), current_num_total_tasks(0), tasks_dispatched(0), tasks_done(0) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //

    workers.reserve(num_threads);
    for(int i = 0; i < num_threads; ++ i) {
        workers.emplace_back(&TaskSystemParallelThreadPoolSleeping::workerLoop, this);
    }

}

TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping() {
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //

    {
        std::unique_lock<std::mutex> lock(mutex_);
        is_killed = true;
    }
    worker_cv.notify_all(); // wake up all sleeping worker thread to see is_killed == true and exit
    for(auto &worker : workers) {
        if(worker.joinable()) worker.join();
    }

}

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    {
        std::unique_lock<std::mutex> lock(mutex_);
        current_runnable = runnable;
        this->current_num_total_tasks = num_total_tasks;
        this->tasks_dispatched = 0;
        this->tasks_done = 0;
    }
    worker_cv.notify_all(); // wake up all waiting worker threads to grab tasks
    {
        std::unique_lock<std::mutex> lock(mutex_);
        main_cv.wait(lock, [this](){
            return this->tasks_done == this->current_num_total_tasks;
        });
    }

    // for (int i = 0; i < num_total_tasks; i++) {
    //     runnable->runTask(i, num_total_tasks);
    // }
}

void TaskSystemParallelThreadPoolSleeping::workerLoop() {
    while(1) {
        int task_id = -1;
        int num_total_tasks = 0;
        IRunnable *runnable = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            worker_cv.wait(lock, [this](){
                return is_killed || tasks_dispatched < current_num_total_tasks;
            });
            if(is_killed && tasks_dispatched >= current_num_total_tasks) return ;
            task_id = tasks_dispatched ++;
            num_total_tasks = current_num_total_tasks;
            runnable = current_runnable;
        }
        if(runnable != nullptr) {
            runnable->runTask(task_id, num_total_tasks);
            {
                std::unique_lock<std::mutex> lock(mutex_);
                tasks_done ++;
                if(tasks_done == num_total_tasks) main_cv.notify_all();
            }
        }
    }
}

TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                    const std::vector<TaskID>& deps) {


    //
    // TODO: CS149 students will implement this method in Part B.
    //

    return 0;
}

void TaskSystemParallelThreadPoolSleeping::sync() {

    //
    // TODO: CS149 students will modify the implementation of this method in Part B.
    //

    return;
}
