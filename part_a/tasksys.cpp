#include "tasksys.h"

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
    threads = new std::thread[num_threads];
}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

void TaskSystemParallelSpawn::run_thread(IRunnable* runnable, int start, int end, int num_total_tasks) {
    for (int i = start; i < end; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {

    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //
    int num_tasks_per_thread = num_total_tasks / num_threads;
    for (int i = 0; i < num_threads; i++) {
        int start = i * num_tasks_per_thread;
        int end = (i + 1) * num_tasks_per_thread;
        if (i == num_threads - 1) {
            end = num_total_tasks;
        }

        threads[i] = std::thread(&TaskSystemParallelSpawn::run_thread, this, runnable, start, end, num_total_tasks);
    }

    for (int i=0; i < num_threads; i++) {
        threads[i].join();
    }
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

void TaskSystemParallelThreadPoolSpinning::run_thread() {
    while (true) {
        if (end_state.load()) {
            break;
        }

        IRunnable* runnable = current_runnable.load();
        if (!runnable) { 
            std::this_thread::yield();
            continue;
        }

        int job = current_job_id.load();
        int total = curr_num_tasks.load();
        
        // Recheck visibility before attempting to reserve
        if (current_runnable.load() != runnable || current_job_id.load() != job)
        {
            std::this_thread::yield();
            continue;
        }

        int task_id = -1;
        while (true) {
            int old = next_task_idx.load();
            if (old >= total) { 
                break;
            }

            // bail if job/runnable changed mid-loop
            if (current_runnable.load() != runnable ||
                current_job_id.load() != job) {
                break;
            }

            // try to claim 'old'
            if (next_task_idx.compare_exchange_weak(old, old + 1)) {
                task_id = old;
                break;
            }
        }

        // int task_id = next_task_idx.fetch_add(1);

        if (task_id < 0) { 
            std::this_thread::yield();
            continue; 
        }

        // if job changed after we claimed, don't run it
        if (current_job_id.load() != job) {
            continue;
        }

        runnable->runTask(task_id, total);
        done_count.fetch_add(1);
    }
}

TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(&TaskSystemParallelThreadPoolSpinning::run_thread, this);
    }
}

// clean up the threads
TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    end_state.store(true);
    current_runnable.store(nullptr);

    for (auto &thread : threads) {
        thread.join();
    }
}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    if (num_total_tasks <= 0) return;
    
    done_count.store(0);
    next_task_idx.store(0);
    curr_num_tasks.store(num_total_tasks);
    
    // Increment job ID to invalidate any in-flight operations
    current_job_id.fetch_add(1);
    current_runnable.store(runnable);
    
    while (done_count.load() < num_total_tasks) {
        std::this_thread::yield();
    }
    
    current_runnable.store(nullptr);
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

TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(&TaskSystemParallelThreadPoolSleeping::run_thread, this);
    }
}

TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping() {
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    end_state.store(true);
    current_runnable.store(nullptr);

    for (auto &thread : threads) {
        thread.join();
    }
}

void TaskSystemParallelThreadPoolSleeping::run_thread() {
    while (true) {
        if (end_state.load()) {
            break;
        }

        IRunnable* runnable = current_runnable.load();
        if (!runnable) { 
            std::this_thread::yield();
            continue;
        }

        int job = current_job_id.load();
        int total = curr_num_tasks.load();
        
        // Recheck visibility before attempting to reserve
        if (current_runnable.load() != runnable || current_job_id.load() != job)
        {
            std::this_thread::yield();
            continue;
        }

        int task_id = -1;
        while (true) {
            int old = next_task_idx.load();
            if (old >= total) { 
                break;
            }

            // bail if job/runnable changed mid-loop
            if (current_runnable.load() != runnable ||
                current_job_id.load() != job) {
                break;
            }

            // try to claim 'old'
            if (next_task_idx.compare_exchange_weak(old, old + 1)) {
                task_id = old;
                break;
            }
        }

        if (task_id < 0) { 
            std::this_thread::yield();
            continue; 
        }

        // if job changed after we claimed, don't run it
        if (current_job_id.load() != job) {
            continue;
        }

        runnable->runTask(task_id, total);
        done_count.fetch_add(1);
    }
}

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
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
