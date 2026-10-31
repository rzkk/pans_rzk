#ifndef     PANS_INCLUDE_PANS_MUTEX_H    // 路径 + _ + 文件名
#define     PANS_INCLUDE_PANS_MUTEX_H

#include<atomic>
#include<mutex>

// ========================== 文件目标 ==========================
// 为项目提供一个名为 pans::Spinlock 的“自旋锁”。
// 自旋锁的特点：拿不到锁时通常不断尝试，不像普通互斥锁那样主动长时间睡眠。
// 适用场景：临界区非常短、竞争不激烈、线程不容易被调度走。
// 不适用场景：长时间持锁、高竞争、线程数超过 CPU 核数等。
//
// 本文件有两个平台版本：
// 1. Linux / glibc：包装 pthread_spinlock_t（操作系统提供的 POSIX API）。
// 2. 其他平台：使用 C++20 的 std::atomic_flag 实现自旋锁。
//
// 注意：这里仅为学习示例；生产级封装还需要处理 pthread API 返回的错误码。

#if defined(__x86_64__) || defined(__i386__)
#include<immintrin.h>  // x86 的 _mm_pause() 指令内在函数
#elif  defined(__aarch64__) || defined(__arm__)
#include<arm_acle.h>
#endif

// 在 Linux/glibc 环境中使用 POSIX 线程自旋锁。
// 注意：宏名 __linux__ / __GLIBC__ 由具体编译环境决定；并非 C++ 标准的一部分。
#if defined(__linux) || defined(__GLIBC__)
#include<pthread.h>
#endif

// CPU 忙等提示：
// 这不是“解锁”，也不是 std::this_thread::yield() 的“让出线程调度”。
// 它只是告诉处理器当前处于一个很短的自旋等待循环，可能减少功耗/执行资源竞争。
//
// inline：函数定义放在头文件时可以在多个 .cpp 中使用，不会导致重复定义错误。
// noexcept：承诺本函数不会抛出 C++ 异常。
// [可改进] 全局函数可以移入 pans::detail 命名空间，避免污染全局命名空间。
inline void cpu_relax() noexcept{
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(__aarch64__) || #defined(__arm__)
    __yield();
#endif
}

namespace pans
{


#if defined(__linux__) || defined(__GLIBC__)
// pthread 版本 : 直接封装
//noexcept : 承诺不抛异常 
// 这是对 pthread_spinlock_t 的 C++ RAII 风格包装。
// 为什么提供 lock() 和 unlock()？
// std::lock_guard<T> 要求 T 满足 BasicLockable：必须有 lock() / unlock()。
// 因此lock_guard 无需自己手动写 unlock()，直接使用 std::lock_guard<pans::Spinlock>。
class Spinlock{
public:
    // 构造时初始化底层锁。
    // PTHREAD_PROCESS_PRIVATE：锁仅用于同一进程内的不同线程。
    // 注意：pthread_spin_init 会返回错误码；这里保留仓库原逻辑，未检查返回值。
    //       生产代码应判断初始化是否成功，不能假设永远成功。
    Spinlock() noexcept{
        pthread_spin_init(&m_mutex , PTHREAD_PROCESS_PRIVATE); // 
    }
    // RAII：对象析构时销毁底层资源。
    // 前提：此时已没有线程持有或等待这个锁，否则行为有问题。
    ~Spinlock() noexcept{
        pthread_spin_destroy(&m_mutex);
    }
    // 锁不应该被复制：复制底层同步状态会破坏多个线程对同一把锁的约定。
    Spinlock(const Spinlock &) = delete;
    Spinlock& operator= (const Spinlock &) = delete;
    
     // 拿锁：如果锁被其他线程持有，pthread_spin_lock 会等待并反复尝试。
    // 注意：仍未检查 pthread_spin_lock 的返回码；此处只保留原项目语义。
    void lock() noexcept{
        pthread_spin_lock(&m_mutex);
    }
    void unlock() noexcept{
        pthread_spin_unlock(&m_mutex);
    }

private:
    pthread_spinlock_t m_mutex{};  // {}：进行值初始化；实际可用状态来自 pthread_spin_init()。
};
#else 
// =================== 非 Linux：C++20 原子自旋锁 ====================
// 用一个仅保存“已上锁 / 未上锁”状态的原子标志实现锁。
// false = 没人持有；true = 已被某个线程抢到。
//Test-Test-And-Set（TTAS） 自旋锁实现方式
class Spinlock{
public:
    // 类型别名：可以写 Spinlock::Lock guard(lock_obj)。
    // 注意：Linux pthread 分支暂未定义这个别名，跨平台使用时要统一接口。
    using Lock = std::lock_guard<Spinlock>;

    // = default：请求编译器生成默认构造、析构函数。
    // noexcept：这些操作不应该抛出异常。
    Spinlock() noexcept = default;
    ~Spinlock() noexcept = default;

    Spinlock(const Spinlock &) = delete;
    Spinlock& operator= (const Spinlock &) = delete;

    void lock() noexcept{
        // test_and_set() 会“原子地设为 true”，同时返回修改之前的值。
        // 若旧值为 false（未锁定）→ 抢锁成功，跳出循环
        // 若旧值为 true（已锁定）→  抢锁失败，进入内层,进入循环继续等待。
        // acquire：拿锁成功后，临界区中的读写不能重排到锁获取之前；
        //          与其他线程 unlock() 的 release 配对，保证看到其写入。
        while(m_mutex.test_and_set(std::memory_order_acquire)){  // 抢锁
            // C++20 新增 atomic_flag::test()：只读当前标志，不修改它。
            // 这里先“观察”锁有没有释放，不必每次都做代价较高的
            // test_and_set() 读-改-写原子操作。
            //
            // relaxed 足够：这个内层循环只负责等状态变化，不访问受锁保护的数据
            // ；真正获取锁时，外层 test_and_set(acquire) 负责同步。
            //内while 避免在高竞争下，每一个等待线程都不断执行原子读-改-写操作。
            while( m_mutex.test(std::memory_order_relaxed) ){   // 看看锁释放没 ， 要不要去抢一下
                cpu_relax();  // 告诉cpu , 在忙等 ， 
            }
             // 看见 false 后回到外层重新争抢；看见空闲不等于已经拥有锁。
            // 在我们争抢之前，别的线程可能先一步拿走。
        }
    }

    
    // release：保证本线程在临界区的写入先于“解锁”对其他线程可见。
    // 下一位线程 acquire 成功后才能安全观察到这些写入。
    void unlock() noexcept{
       m_mutex.clear(std::memory_order_release);
    }

     // 尝试一次，不阻塞：成功返回 true，失败立即返回 false。
    // [[nodiscard]]：提醒调用者别无意丢弃“是否拿到锁”的结果。   // [[nodiscard]] 这个函数的返回值很重要，不应该随意忽略。
    [[nodiscard]] bool try_lock() noexcept{
        return !m_mutex.test_and_set( std::memory_order_acquire );
    }
private:
    // ATOMIC_FLAG_INIT 初始化为清除状态（false，即“未上锁”）。
    // 不能用普通 bool：普通 bool 的竞争读写会发生 data race（数据竞争）。
    std::atomic_flag m_mutex = ATOMIC_FLAG_INIT;
};


// acquire ：成功加锁时使用 ， 不仅保证锁状态的原子操作，还与之前的 release 配合建立跨线程同步关系。
// relaxed：轮询时只需要relaxed , 不需要承担受保护数据的同步职责
//release：解锁时使用, 将临界区内的写入发布给后续通过匹配的 acquire 成功获取锁的线程。


#endif

} //  namespace pans





#endif
