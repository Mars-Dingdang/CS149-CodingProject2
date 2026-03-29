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
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }

    return 0;
}

void TaskSystemSerial::sync() {
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
    // NOTE: CS149 students are not expected to implement TaskSystemParallelSpawn in Part B.
}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelSpawn in Part B.
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                 const std::vector<TaskID>& deps) {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelSpawn in Part B.
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }

    return 0;
}

void TaskSystemParallelSpawn::sync() {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelSpawn in Part B.
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

TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads): ITaskSystem(num_threads) {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelThreadPoolSpinning in Part B.
}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelThreadPoolSpinning in Part B.
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelThreadPoolSpinning in Part B.
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }

    return 0;
}

void TaskSystemParallelThreadPoolSpinning::sync() {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelThreadPoolSpinning in Part B.
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

TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads): ITaskSystem(num_threads), num_threads_(num_threads), is_killed(false), next_launch_id(0), unfinished_launches(0) {
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
    worker_cv.notify_all();
    for(auto &worker : workers) {
        if(worker.joinable()) worker.join();
    }
}

void TaskSystemParallelThreadPoolSleeping::enqueueLaunchTasks(TaskID launch_id) {
    if(!launches.count(launch_id)) return;
    auto &launch = launches[launch_id];
    int n = launch.num_total_tasks;
    for(int i = 0; i < n; ++ i) {
        ready_tasks.push_back({launch_id, i});
    }
}

void TaskSystemParallelThreadPoolSleeping::finishLaunch(TaskID launch_id) {
    if(!launches.count(launch_id)) return ;
    auto &launch = launches[launch_id];
    if(launch.is_finished) return; 
    launch.is_finished = true;
    unfinished_launches --;
    for(auto &child_id: launch.dependencies) {
        if(!launches.count(child_id)) continue;
        auto &child = launches[child_id];
        child.unmet_deps --;
        if(child.unmet_deps == 0) {
            if(child.num_total_tasks == 0) finishLaunch(child_id);
            else enqueueLaunchTasks(child_id);
        }
    }
    if(unfinished_launches == 0) main_cv.notify_all();
    worker_cv.notify_all();
}

void TaskSystemParallelThreadPoolSleeping::workerLoop() {
    while(1) {
        std::pair<TaskID, int> item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            worker_cv.wait(lock, [this](){
                return this->is_killed || !this->ready_tasks.empty();
            });
            if(is_killed && ready_tasks.empty()) return;
            item = ready_tasks.front();
            ready_tasks.pop_front();
        }
        TaskID launch_id = item.first;
        int task_id = item.second;
        IRunnable* runnable = nullptr;
        int num_total_tasks = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(!launches.count(launch_id)) continue;
            auto &launch = launches[launch_id];
            runnable = launch.runnable;
            num_total_tasks = launch.num_total_tasks;
        }
        runnable->runTask(task_id, num_total_tasks);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(launches.count(launch_id) == 0) continue;
            auto &launch = launches[launch_id];
            launch.tasks_done ++;
            if(launch.tasks_done == launch.num_total_tasks) {
                finishLaunch(launch_id);
            }
        }

    }
}

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    std::vector<TaskID> deps;
    runAsyncWithDeps(runnable, num_total_tasks, deps);
    sync();
    

    // for (int i = 0; i < num_total_tasks; i++) {
    //     runnable->runTask(i, num_total_tasks);
    // }
}



TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                    const std::vector<TaskID>& deps) {


    //
    // TODO: CS149 students will implement this method in Part B.
    //

    // auto completion 
    std::unique_lock<std::mutex> lock(mutex_);

    TaskID launch_id = next_launch_id ++;
    LaunchState launch_state = {launch_id, runnable, num_total_tasks, 0, 0, false};
    launches[launch_id] = launch_state;
    unfinished_launches ++;
    for(auto dep_id : deps) {
        if(!launches.count(dep_id)) continue;
        if(launches[dep_id].is_finished) continue;
        launches[dep_id].dependencies.push_back(launch_id);
        launches[launch_id].unmet_deps ++;
    }
    if (launches[launch_id].unmet_deps == 0) {
        if(num_total_tasks == 0) finishLaunch(launch_id);
        else {
            enqueueLaunchTasks(launch_id);
            worker_cv.notify_all();
        }
    }
    return launch_id;

    
    // for (int i = 0; i < num_total_tasks; i++) {
    //     runnable->runTask(i, num_total_tasks);
    // }

    // return 0;
}

void TaskSystemParallelThreadPoolSleeping::sync() {

    //
    // TODO: CS149 students will modify the implementation of this method in Part B.
    //
    std::unique_lock<std::mutex> lock(mutex_);
    main_cv.wait(lock, [this](){
        return this->unfinished_launches == 0;
    });

    return;
}
