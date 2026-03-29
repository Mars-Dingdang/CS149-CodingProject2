# 并行计算 Coding Project 2 教程
## 任务一：实现多线程并行
任务一修改了 'TaskSystemParallelSpawn`，在 `run()` 方法中直接创建了 `num_threads` 个线程来执行任务，最终同步所有线程。其代码如下：

```cpp
void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {
    std::thread workers[num_threads];
    auto workerLoop = [&](int thread_id) {
        for(int i = thread_id; i < num_total_tasks; i += num_threads) {
            runnable->runTask(i, num_total_tasks);
        }
    };
    for (int i = 1; i < num_threads; ++ i) 
        workers[i] = std::thread(workerLoop, i);
    workerLoop(0);
    for (int i = 1; i < num_threads; ++ i)
        workers[i].join();
}
```

其中，创建线程使用了函数 `std::thread`，其用法如下：

```cpp
std::thread t(function, arg1, arg2, ...);
```
`std::thread` 的构造函数接受一个可调用对象（函数指针、lambda 表达式、函数对象等）和该可调用对象的参数。创建线程后，线程会立即开始执行指定的函数。在上面的代码中，我们定义了一个 lambda 表达式 `workerLoop`，它接受一个线程 ID 作为参数，并在循环中执行任务。我们为每个线程创建一个 `std::thread` 对象，并传入 `workerLoop` 和线程 ID 作为参数。最后，我们调用 `join()` 方法等待所有线程完成执行。

这样的多线程并发实现利用了 Coding Project 1 中类似的任务分配模式，但是问题在于每次调用 `run()` 都会创建新的线程，这会导致性能问题，尤其是在频繁调用 `run()` 的情况下。因此，在任务二中，我们将实现一个线程池来重用线程，从而提高性能。

## 任务二：实现 Spinning 线程池
任务二要求我们实现一个线程池来重用线程，避免频繁创建和销毁线程带来的性能开销。我们将实现 `TaskSystemParallelThreadPoolSpinning` 类，该类在构造函数中创建固定数量的线程，并在 `run()` 方法中将任务分配给线程池中的线程执行。线程池中的线程将持续运行，等待新的任务到来，并在完成任务后继续等待下一个任务。

为此，我们用一个 `std::vector<std::thread>` 来存储线程池中的线程，初始化创建 
```cpp
workers.reserve(num_threads);
for (int i = 0; i < num_threads; ++i) {
    workers.emplace_back(&TaskSystemParallelThreadPoolSpinning::workerLoop, this, i);
}
```
在解构函数中，我们需要确保所有线程都正确地退出。为此，我们需要保证 `is_killed` 是原子操作，可以使用 `std::atomic<bool>` 来实现线程安全的标志。

```cpp
~TaskSystemParallelThreadPoolSpinning() {
    is_killed = true;
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
```

对于 `workerLoop` 方法，我们需要实现一个循环来等待任务的到来，并在有任务时执行它们。由于每次调用 `run` 返回前都会完成所有任务，我们只需要维护原子变量 `std::atomic<int> num_total_tasks, next_task_id` 来跟踪当前任务即可。

```cpp
void TaskSystemParallelThreadPoolSpinning::workerLoop(int thread_id) {
    while (!is_killed) {
        int task_id = next_task_id.fetch_add(1);
        if(current_runnable != nullptr && task_id < num_total_tasks) {
            current_runnable->runTask(task_id, num_total_tasks);
            tasks_done.fetch_add(1);
        }
        else {
            std::this_thread::yield(); // 让出 CPU 时间片，避免忙等待
        }
    }
}
```

在 `run()` 方法中，我们通过更改 `this` 指针发布任务，并且让主线程等待所有任务完成：

```cpp
void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    current_runnable = runnable;
    this->num_total_tasks = num_total_tasks;
    next_task_id = 0;
    tasks_done = 0;

    while (tasks_done.load() < num_total_tasks) {
        std::this_thread::yield(); // 让出 CPU 时间片，避免忙等待
    }

    this->current_runnable = nullptr; // 任务完成后重置 runnable 指针
    this->num_total_tasks = 0;
}
```

## 任务三：实现 Sleeping 线程池
任务二的问题在于，当没有任务时，线程会持续占用 CPU 资源进行忙等待。为了避免这种情况，我们在任务三中实现一个 Sleeping 线程池，当没有任务时，线程会进入睡眠状态，直到有新的任务到来。我们将使用 `std::condition_variable` 来实现线程的睡眠和唤醒机制，并且使用一个 `std::mutex` 来保护共享资源的访问。

### 语法的使用
具体的语法如下：

1. 互斥锁 `std::mutex` 用于保护共享资源的访问，确保同一时间只有一个线程可以访问共享资源。其含有成员函数 `lock()` 和 `unlock()` 来获取和释放锁，但通常使用 `std::unique_lock<std::mutex>` 来自动管理锁的生命周期。
```cpp
bool is_killed = false; // 共享资源
std::mutex mutex_; // 互斥锁
{
    std::unique_lock<std::mutex> lock(mutex_); // 自动获取锁
    is_killed = true;// 访问共享资源
} // 离开作用域时自动释放锁

// 等价于 
// std::atomic<bool> is_killed = false; 
// is_killed = true;
// 使用 mutex 的优势在于它可以保护更复杂的数据结构，而不仅仅是单个原子变量。

// 手动管理锁
std::mutex mutex_;
mutex_.lock();
is_killed = true; // 访问共享资源
mutex_.unlock(); // 必须确保在所有代码路径中都调用 unlock()，否则可能导致死锁。
```
2. 条件变量 `std::condition_variable` 用于线程之间的通知机制，允许一个线程等待某个条件的发生，并且在条件满足时被另一个线程唤醒。其成员函数 `wait()` 用于等待条件，`notify_one()` 和 `notify_all()` 用于唤醒一个或所有等待的线程。
```cpp
std::condition_variable worker_cv; // 条件变量
std::mutex mutex_;
bool has_task = false; // 共享资源
// 工作线程
void worker() {
    std::unique_lock<std::mutex> lock(mutex_);
    worker_cv.wait(lock, [](){ return has_task; }); // 等待条件满足
    // 执行任务
}
// 主线程
void main() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        has_task = true; // 更新共享资源
    }
    worker_cv.notify_all(); // 唤醒一个等待的线程
    // 或者 worker_cv.notify_one(); // 唤醒一个等待的线程
}
```
其中，`wait(lock, predicate)` 会首先检查 `predicate` 是否为真，如果为假，则线程会进入睡眠状态并释放 `lock`，直到另一个线程调用 `notify_one()` 或 `notify_all()` 唤醒它，并且在被唤醒后重新获取 `lock`。如果条件为真，则线程会立刻抢占 `lock`。使用条件变量可以有效地避免忙等待，提高程序的效率。

### 代码实现
我们维护 `tasks_dispatched` 和 `tasks_done` 来跟踪已分配和已完成的任务数量，并且在 `run()` 方法中使用条件变量来等待所有任务完成：初始化函数类似于上一个任务，同样是创建一个线程池。

对于解构函数，我们用互斥锁控制线程的退出，确保所有线程都正确地退出：同时，由于线程结束的时候需要让每一个线程知道并且唤醒他们然后使其退出，需要使用条件变量来通知所有线程退出。

```cpp
{
    std::unique_lock<std::mutex> lock(mutex_);
    is_killed = true;
}
worker_cv.notify_all();
for(auto &worker : workers) { 
    if(worker.joinable()) worker.join();
}
```

`run()` 方法类似，我们在发布任务之后用条件变量通知所有的线程来抢任务，而主线程则等待所有任务都被完成。

```cpp
{
    std::unique_lock<std::mutex> lock(mutex_);
    current_runnable = runnable;
    this->current_num_total_tasks = num_total_tasks;
    this->tasks_dispatched = 0;
    this->tasks_done = 0;
}
worker_cv.notify_all(); // 唤醒所有线程来抢任务
{
    std::unique_lock<std::mutex> lock(mutex_);
    main_cv.wait(lock, [this]() {
        return this->tasks_done >= this->current_num_total_tasks;
    }); // 等待所有任务完成
}
```

在 `workerLoop` 中，我们同样使用无限循环，但这一次我们不再使用 `std::this_thread_yield()` 来忙等待，而是使用条件变量控制休眠。 

```cpp
while(1) {
    int task_id = -1; // no task
    int num_total_tasks = 0;
    IRunnable* runnable = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        worker_cv.wait(lock, [this](){
            return this->is_killed || tasks_dispatched < current_num_total_tasks;
        }); // 等待有任务可做或者线程池被销毁
        if(is_killed && tasks_dispatched >= current_num_total_tasks) return; 
        task_id = tasks_dispatched++;
        num_total_tasks = current_num_total_tasks;
        runnable = current_runnable;
    }
    if(runnable != nullptr && task_id < num_total_tasks) {
        runnable->runTask(task_id, num_total_tasks);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_done++;
            if(tasks_done >= current_num_total_tasks) main_cv.notify_all(); // 唤醒主线程等待任务完成
        }   
    }
}
```

## 任务四：实现带依赖关系的线程池
实际上，任务三的实现逻辑可以套用在任务四之上。只不过，我们需要维护依赖关系图上每个节点的信息（用 `struct LaunchState` 来表示），并且用 `unordered_map<TaskID, LaunchState>` 来存储每个任务的信息。每当一个任务完成时，我们需要检查它的依赖关系图上的后续节点是否满足条件，如果满足条件则使用 `enqueueLaunchTasks` 将其加入到可执行任务队列中，否则如果已结束，使用 `finishLaunch`。我们可以使用一个 `std::deque<std::pair<TaskID, int>>` 来维护当前可执行的任务队列，当一个任务完成后，我们检查它的后续节点，如果满足条件则将其加入到队列中。线程池中的线程将从这个队列中获取任务来执行。

```cpp
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
```