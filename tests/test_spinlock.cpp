#include <pans/spinlock.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <pans/macros.h>

constexpr int RUNS = 7;
u64 g_Counter = 0;
u64 g_target = 0;

class Spinlock
{
public:
    using Lock = std::lock_guard<Spinlock>;

    Spinlock() noexcept = default;
    ~Spinlock() noexcept = default;

    Spinlock(const Spinlock&) = delete;
    Spinlock& operator=(const Spinlock&) = delete;

    void lock() noexcept
    {
        while (m_mutex.test_and_set(std::memory_order_acquire))
        {
            while (m_mutex.test(std::memory_order_relaxed))
            {
                cpu_relax();
            }
        }
    }

    [[nodiscard]] bool try_lock() noexcept
    {
        return !m_mutex.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept
    {
        m_mutex.clear(std::memory_order_release);
    }

private:
    std::atomic_flag m_mutex = ATOMIC_FLAG_INIT;
};

// ====================== 方案 2：atomic_wait 等待式锁 ======================
// C++20 提供 atomic_wait / atomic_notify_one。
// 它与上面的“纯自旋”不同：等待时可能让当前线程进入阻塞状态，
// 从而节约 CPU，但阻塞/唤醒有额外开销。
// 注意：具体是否用操作系统等待原语属于标准库的实现细节。
// 当前 main() 中这一项被注释，没有参加本次实际对比。
class AtomicWaitLock
{
public:
    using Lock = std::lock_guard<AtomicWaitLock>;

    AtomicWaitLock() noexcept = default;
    ~AtomicWaitLock() noexcept = default;

    AtomicWaitLock(const AtomicWaitLock&) = delete;
    AtomicWaitLock& operator=(const AtomicWaitLock&) = delete;

    void lock() noexcept
    {   // false=空闲，true=被占用。
        // CAS 操作需要一个可修改的 expected 变量，作为“期望的旧值”。
        bool expected = false;
        // compare_exchange_weak(expected, true, success_order, failure_order)：
        // 若当前状态==expected(false)，就把状态原子地改成 true，取得锁； 返回true , 不进入循环
        // 否则失败，将实际状态写回 expected。
        // weak 允许伪失败，因此必须放在循环里重试。
        while (!m_mutex.compare_exchange_weak(   // 尝试获取锁
                    expected, true, 
                    std::memory_order_acquire,  // 成功拿锁后获得临界区同步保障。
                    std::memory_order_relaxed)) // 未拿到锁时不需要同步其他业务数据。
        {
            // 等待 m_mutex 不再等于 true。
            // 这不是普通 bool 的条件等待，而是对原子对象的等待。
            // 可能被虚假唤醒，或醒来后锁已被其他线程抢走，
            // 所以外层 while 一定要重新尝试 CAS。
            //函数： 当一个原子变量的值等于指定的旧值（old）时，让当前线程等待；
            //收到通知后重新检查，直到该值不再等于 old 才返回。
            //void std::atomic_wait_explicit(
                    // const std::atomic<T>* object,
                    // typename std::atomic<T>::value_type old,
                    // std::memory_order order）
            std::atomic_wait_explicit(&m_mutex, 
                true,                   //只要锁仍然是 true（被占用），就继续等待它变化。
                std::memory_order_relaxed);
            
            // CAS 失败时 expected 通常被写成“实际值 true”。
            // 下一次争抢必须重新把期望状态设为 false，
            // 否则可能错误地尝试把 true->true 并当作已获取锁。
            expected = false;
        }
    }

    [[nodiscard]] bool try_lock() noexcept
    {
        bool expected = false;
        return m_mutex.compare_exchange_strong(expected, true, std::memory_order_acquire, std::memory_order_relaxed);
    }

    void unlock() noexcept
    {
        // 先 release 发布被保护的数据，再唤醒一个等待中的线程。
        // 若只 notify 而没有 store(false)，等待者醒来仍然拿不到锁。
        m_mutex.store(false, std::memory_order_release);
        std::atomic_notify_one(&m_mutex);
    }

private:
    std::atomic<bool> m_mutex{false};
};

u64 OperationsForThread(std::size_t thread_index, std::size_t thread_count)
{
    const u64 base = g_target / thread_count;
    const u64 remainder = g_target % thread_count;
    return base + (thread_index < remainder ? 1 : 0);
}

template <typename Operation>
std::chrono::nanoseconds BenchmarkThreads(std::size_t thread_count, Operation&& operation)
{
    std::latch ready(static_cast<std::ptrdiff_t>(thread_count));
    std::latch start_gate(1);
    std::latch finished(static_cast<std::ptrdiff_t>(thread_count));

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index)
    {
        threads.emplace_back([&, thread_index]() {
            ready.count_down();
            start_gate.wait();
            operation(thread_index, OperationsForThread(thread_index, thread_count));
            finished.count_down();
        });
    }

    ready.wait();
    const auto begin = std::chrono::steady_clock::now();
    start_gate.count_down();
    finished.wait();
    const auto end = std::chrono::steady_clock::now();

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
}

template <typename Mutex>
std::chrono::nanoseconds BenchmarkMutex(std::size_t thread_count)
{
    Mutex mutex;
    g_Counter = 0;
    const auto elapsed = BenchmarkThreads(thread_count, [&mutex](std::size_t, u64 operations) {
            for(u64 index = 0; index < operations; ++index)
            {
                std::lock_guard<Mutex> lock(mutex);
                ++g_Counter;
            }
        });

    if (g_Counter != g_target)
    {
        throw std::runtime_error("mutex correctness check failed");
    }

    return elapsed;
}

template <typename Benchmark>
double Run(Benchmark&& benchmark)
{
    long double total_nanoseconds = 0.0L;
    for (int run = 0; run < RUNS; ++run)
    {
        total_nanoseconds += static_cast<long double>(benchmark().count());
    }
    return static_cast<double>(total_nanoseconds / RUNS);
}

void PrintResult(std::string_view name, double total_nanoseconds)
{
    const double nanoseconds_per_operation = total_nanoseconds / static_cast<double>(g_target);
    std::cout << std::left << std::setw(30) << name << std::right
              << total_nanoseconds << " ns \ttotal, "
              << nanoseconds_per_operation << " ns/op\n";
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: ./test_mutex count\n";
        return EXIT_FAILURE;
    }
    g_target = std::stoull(argv[1]);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "operations: " << g_target << ", average of " << RUNS << " runs\n\n";

    const std::vector<std::size_t> thread_counts = {1, 2, 3, 4, 5,6,7,8,10,16,20};
    for (const std::size_t thread_count : thread_counts)
    {
        std::cout << "threads: " << thread_count << '\n';
        PrintResult("pthread_spinlock::Spinlock", Run([&]() { return BenchmarkMutex<pans::Spinlock>(thread_count); }));
        PrintResult("atomic_flag::Spinlock", Run([&]() { return BenchmarkMutex<Spinlock>(thread_count); }));
         PrintResult("atomic_wait", Run([&]() { return BenchmarkMutex<AtomicWaitLock>(thread_count); }));
        PrintResult("std::mutex", Run([&]() { return BenchmarkMutex<std::mutex>(thread_count); }));
        std::cout << '\n';
    }

    return EXIT_SUCCESS;
}

/**
 *
感谢B站网友：青烟长弧  提供的使用std::atomic_wait实现自旋锁的思路
实测数据如下(平台6核 x86_64 Debian机器)：

dev@debian:~/Code/second/pans/bin/tests$ ./test_mutex 1000000
operations: 1000000, average of 7 runs 

threads: 1
pthread_spinlock::Spinlock    14335512.29 ns    total, 14.34 ns/op
atomic_flag::Spinlock         15890191.71 ns    total, 15.89 ns/op
atomic_wait                   22092990.57 ns    total, 22.09 ns/op
std::mutex                    25561656.00 ns    total, 25.56 ns/op

threads: 2
pthread_spinlock::Spinlock    58567846.29 ns    total, 58.57 ns/op
atomic_flag::Spinlock         57283615.14 ns    total, 57.28 ns/op
atomic_wait                   32876545.57 ns    total, 32.88 ns/op
std::mutex                    59783068.14 ns    total, 59.78 ns/op

threads: 4
pthread_spinlock::Spinlock    107808279.29 ns   total, 107.81 ns/op
atomic_flag::Spinlock         77783261.57 ns    total, 77.78 ns/op
atomic_wait                   78707336.14 ns    total, 78.71 ns/op
std::mutex                    65705970.43 ns    total, 65.71 ns/op

threads: 8
pthread_spinlock::Spinlock    172506586.29 ns   total, 172.51 ns/op
atomic_flag::Spinlock         123846987.86 ns   total, 123.85 ns/op
atomic_wait                   215281753.43 ns   total, 215.28 ns/op
std::mutex                    81340459.29 ns    total, 81.34 ns/op

threads: 10
pthread_spinlock::Spinlock    203061361.57 ns   total, 203.06 ns/op
atomic_flag::Spinlock         168216887.14 ns   total, 168.22 ns/op
atomic_wait                   238041247.14 ns   total, 238.04 ns/op
std::mutex                    84330990.43 ns    total, 84.33 ns/op
 
结论：

单核基本消耗：                  pthread_spinlock更优秀，atomic_flag与之旗鼓相当且有更好的移植性
线程数较少时(2线程)：           atomic_wait性能更好，消耗几乎是其它几种方案的一半
线程数较多(4线程)：             std::mutex性能更优异，更稳定，其它几种方案出现了明显的性能退化趋势
线程数大于核心数时(8线程10线程)：std::mutex更稳定，其它几种方案性能成倍恶化

基于此结果，在后续选择锁的场景中，我会选用std::mutex, 会谨慎选择自旋锁

 * 
 */
