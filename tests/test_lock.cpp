#include <atomic>   // std::atomic、memory_order_relaxed
#include <charconv> // 当前版本未使用；以后可用 std::from_chars 替代 atoi
#include <chrono>   // std::chrono::steady_clock / nanoseconds
#include <cstddef>      // std::size_t、std::ptrdiff_t
#include <cstdint>      // std::uint64_t（补齐直接依赖）
#include <cstdlib>     // std::atoi（补齐直接依赖）

#include <iomanip>    // std::fixed、std::setprecision、std::setw   
#include <iostream>
#include <latch>        // C++20 std::latch：一次性线程同步门闩
#include <mutex>            // std::mutex、std::lock_guard、std::unique_lock
#include <numeric>          // 当前版本未使用
#include <shared_mutex>   // std::shared_mutex
#include <stdexcept>// 当前版本未使用
#include <string_view>   // std::string_view：非拥有式字符串视图
#include <thread>
#include <vector>

#include <pans/macros.h>   // 项目头文件，提供 u64 等类型别名

//constexpr：编译期常量；每种方案测量 7 次，最终求平均。
constexpr int RUNS = 7;

// g_target：一轮测试中，所有线程加起来需要执行的总次数。
uint64_t g_target = 0;
uint64_t g_counter = 0;   //普通整数，多线程共享变量,只有在所有读写都被同一把互斥锁保护时才安全。
std::atomic<uint64_t> g_atomicCounter{0};

// 把同一把锁共享给所有线程，才能模拟多个线程争抢同一资源。
// 如果每个线程都拥有自己的 mutex，就没有真正的共享竞争了。
std::mutex g_mutex;
std::shared_mutex g_shareMutex;

u64 OperationsForThread(std::size_t thread_index, std::size_t thread_count)
{
    const u64 base = g_target / thread_count;
    const u64 remainder = g_target % thread_count;
    return base + (thread_index < remainder ? 1 : 0);
}

template<typename Operation> 
std::chrono::nanoseconds BenchmarkThreads(std::size_t thread_count , Operation && operation){
    // C++20 std::latch 是“一次性倒计数门闩”：
    //  count_down() 减少计数；
    //  wait() 等计数变为 0 才继续。
    // 和 condition_variable 相比，本场景只需要一次启动同步，latch 更简洁。
    //
    // size_t 通常无符号，latch 构造参数是 ptrdiff_t，所以做显式转换。
    // [前提] thread_count 不超过 ptrdiff_t 能表达的正数范围。
    // ptrdiff_t 有符号 ， 表示指针差值、可能为负的距离 ， 8B 
    // /size_t : 无符号，8B
    std::latch ready(static_cast<std::ptrdiff_t>(thread_count));
    std::latch start_gate(1);
    std::latch finished(static_cast<std::ptrdiff_t>(thread_count));

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    // emplace_back() 在 vector 中直接构造 std::thread。
    // [&, thread_index]：
    //   &  ：其他被使用的局部变量（如 operation、thread_count）按引用捕获；
    //   thread_index：按值捕获，使每个线程都保留自己的序号。
    // 如果全部按引用捕获 thread_index，循环继续修改它会引发数据竞争，
    // 线程也可能拿到错误序号。
    for(std::size_t thread_index = 0; thread_index < thread_count; ++thread_index)
    {
        threads.emplace_back([&, thread_index]() { // 让所有线程都从同一个时刻执行
                                    ready.count_down();  // ready--  // 告诉主线程：我已经启动并到达等待起跑的位置。
                                    start_gate.wait();   // 等待   //所有的子线程都在这里等待 // 等主线程“发令枪”：避免线程创建时间占入计时区间。
                                    // 每个线程执行自己分到的次数。// operation 可能是加锁自增，也可能是原子操作。
                                    operation(thread_index, OperationsForThread(thread_index, thread_count));  // operation 的第一个 thread_index ， 没有用到
                                    finished.count_down();
        });
    }

    ready.wait(); //主线程再等所有子线程都执行了 ready.count_down()  
    const auto begin = std::chrono::steady_clock::now();
    start_gate.count_down();  // 主线程 释放所有子线程 ， 让他们一起开始启动
    finished.wait();          //主线程等待所有子线程完成任务
    const auto end = std::chrono::steady_clock::now();

    for(std::thread& thread : threads)
    {
        thread.join();
    }
    // duration_cast：明确将耗时转换成“纳秒”单位。
    // [注意] 计时仍包含线程唤醒、调度、latch 通知等开销，
    //        并非单次 mutex::lock() 的纯粹延迟。
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);

}

std::chrono::nanoseconds BenchmarkMutex(std::size_t thread_count){
    g_counter = 0;   // 重置发生在线程创建之前，不会与本轮工作线程同时访问 g_counter。
    //BenchmarkThreads : worker 函数 ， 
    //thread_count ： 工人数量
    // lammda : 工人具体干的事
    const auto elapsed = BenchmarkThreads( thread_count , [](std::size_t , uint64_t operations){  // operations ：OperationsForThread提供的， 
                                                                for(uint64_t index = 0 ; index < operations ; ++index){
                                                                    std::lock_guard<std::mutex>lock(g_mutex);
                                                                    ++g_counter;
                                                                }
                                                            } 
                                        );
    return elapsed;
}
std::chrono::nanoseconds BenchmarkSharedMutex(std::size_t thread_count){
    g_counter = 0;
    const auto elapsed = BenchmarkThreads( thread_count , [](std::size_t , uint64_t operations){
                    for(uint64_t index = 0 ; index < operations ; ++index){
                        std::unique_lock<std::shared_mutex>lock(g_shareMutex);
                        ++g_counter;
                    }
                } );
    return elapsed;
}
std::chrono::nanoseconds BenchmarkAtomic(std::size_t thread_count){
    //// memory_order_relaxed  保证“这一个整数上的操作是原子的”，但不建立其他数据的同步顺序。
    g_atomicCounter.store(0, std::memory_order_relaxed); 
    const auto elapsed = BenchmarkThreads( thread_count , [](std::size_t , uint64_t operations){
                    for(uint64_t index = 0 ; index < operations ; ++index){
                        // fetch_add(1)：不可分割的读-改-写操作；多个线程不会互相覆盖增量。
                         // 这是原子性，不代表所有 atomic 操作都不用锁（平台可能内部实现加锁）。
                       g_atomicCounter.fetch_add(1,std::memory_order_relaxed);
                    }
                } );
    return elapsed;
}
std::chrono::nanoseconds BenchmarkAtomicCas(std::size_t thread_count){
    g_atomicCounter.store(0, std::memory_order_relaxed); //??????
    const auto elapsed = BenchmarkThreads( thread_count , [](std::size_t , uint64_t operations){
                    for(uint64_t index = 0 ; index < operations ; ++index){
                        // CAS = Compare-And-Swap（比较并交换）。
                        // 先读到旧值 expected，希望原子地把 expected 改成 expected+1。
                        // compare_exchange_weak(expected, desired, success_order, failure_order)：
                        //   如果当前值 == expected，则把它写为 desired，返回 true。
                        //   如果不相等，则返回 false，并把“实际当前值”写回 expected。
                        //   weak 即便相等也允许偶发地返回 false（伪失败），所以放入循环重试。
                        //
                        // 失败后 expected 会被更新；下一次计算 expected + 1 即可重新尝试。
                        // 这里只统计单个原子计数器，不传递其他共享数据，用 relaxed 足够。
                        // [注意] 高竞争时 CAS 可能反复失败，带来额外的缓存一致性开销。
                       uint64_t expected = g_atomicCounter.load(std::memory_order_relaxed);
                       while(!g_atomicCounter.compare_exchange_weak(expected , expected+1 , std::memory_order_relaxed ,std::memory_order_relaxed )){

                       }
                    }
                } );
    return elapsed;
}
//每次 benchmark() 返回纳秒 duration；count() 提取整数纳秒值。
// 用 long double 累加再转 double，减轻多次求和的精度损失。
//long double : 也是double , 表示至少不低于 double 
template<typename Benchmark> // 负责跑传入的函数 Benchmark : 传入lamda 函数 
double Run(Benchmark && benchmark){
    long double total_nanoseconds = 0.0L;
    for(int run = 0 ; run < RUNS ; ++run){
        total_nanoseconds += static_cast<long double>(benchmark().count());  //benchmark() : 调用了这个 lammda 函数
    }
    return static_cast<double> (total_nanoseconds / RUNS);
}

// 按固定列宽打印一个结果；参数是“ 7 次测试的平均总耗时（纳秒） ”。
void PrintResult(std::string_view name , double total_nanoseconds){

    // static_cast ：显式类型转换运算符 ,  告诉编译器：“我知道这里的数据类型需要转换，请按照我指定的类型进行转换。”
            //类型转换意图更明确。 在代码中更容易搜索和审查。
    // ns/op = 全部线程共同完成 g_target 次操作所用时间 / 总操作数。
    // 这是“聚合吞吐成本指标”，不是某个线程一次锁获取的精确延迟。
    const double nanoseconds_per_operation = total_nanoseconds / static_cast<double>(g_target);

    // setw 表示紧接着输出的字段最小宽度为 14 个字符， 只对紧接着的一个输出字段有效，不会一直生效。  // name 的名字左对齐 ， 14个字符， 不够的补空格
    //left/right 控制对齐，便于比较多行结果。
    std::cout<<std::left << std::setw(14) <<name 
         << total_nanoseconds <<" ns \ttotal , "
        << nanoseconds_per_operation <<" ns /op\n";
}


//   std::vector<std::thread> threads;

int main(int argc ,char ** argv){
    if(argc != 2 ) {
        std::cerr << "Usage : ./test_lock count \n" ;
        return -1;
    }

    g_target = std::atoi(argv[1]);
    /*std::fixed : 告诉cout 输出浮点数时采用固定小数表示形式，而不是根据数值大小自动选择普通或科学计数法
        std::setprecision(2)：设置精度 , 单独使用默认是 保留两位有效数字 ， 如  3.1415926; ->   3.1    123.45678 —>  1.2e+02
        std::fixed + std::setprecision(2) : 保留小数点后 2 位  如 ：   3.1415926; ->   3.14    123.45678 —>  123.46
        这两个设置会保留在 cout 的输出状态中，并不会在输出一次后自动失效。
        它们主要影响浮点数，对普通整数的输出不产生这种小数位数的效果
    */
    std::cout<< std::fixed << std::setprecision(2);  // 输出保留两位小数点 
    std::cout << "operations: " << g_target << ", average of " << RUNS << " runs\n\n";

    // 不同线程数：观察无竞争（1 个线程）与不同竞争程度的变化。
    // 超过 CPU 核数时，锁竞争与操作系统调度通常会更明显。
    const std::vector<std::size_t> thread_counts{1,2,4,8,10};

    for(const std::size_t thread_count : thread_counts){
        std::cout << "threads: " << thread_count << '\n'; // 线程个数

        // [&]：按引用捕获 thread_count，然后传给各个 Benchmark 函数。
        // Run 接收 lambda，因此同一个计时/求平均逻辑可复用于四种方案。
        PrintResult("mutex",            Run(    [&]() { return BenchmarkMutex(thread_count); }));
        PrintResult("shared mutex",     Run(    [&]() { return BenchmarkSharedMutex(thread_count); }));
        PrintResult("atomic",           Run(    [&]() { return BenchmarkAtomic(thread_count); }));
        PrintResult("atomic CAS",       Run(    [&]() { return BenchmarkAtomicCas(thread_count); }));
        std::cout << '\n';
    }
  
    return 0;
}