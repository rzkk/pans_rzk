#include <array>
#include <atomic>
#include <chrono>
#include <cstddef> // std::size_t 
#include <iomanip> // setw
#include <iostream>
#include <sstream> // std::stringstream
#include <string>
#include <string_view>

#include "logger/include/log_buffer.h"
#include "logger/include/log_buffer_config.h"

#include <pans/macros.h>

constexpr int RUNS = 7;
std::uint64_t g_target= 0 ;  //运行次数

// 定义一个BENCHMARK_SINK变量，收集测量数据，让它产生一个编译器不可忽略的外部副作用，避免编译器把我们的逻辑优化掉
std::atomic<std::uint64_t> BENCHMARK_SINK{0}; // 因为代码如果没有用到的话 ，会被优化掉

//测试性能时 ，要把编译器优化考考虑在内 ， release 会开O2 优化 ， 很智能 ， 防止我们的代码会编译器优化掉
std::uint64_t Observe(std::string_view value) noexcept{
    return static_cast<std::uint64_t>(value.size()) +
           static_cast<unsigned char>(value.front()) +
           static_cast<unsigned char>(value[value.size() / 2]) +
           static_cast<unsigned char>(value.back());
}

std::uint64_t WriteWithSmallStreamBuffer(std::string_view message){
    pans::detail::InlineBuffer<pans::detail::LOG_MESSAGE_INLINE_CAPACITY> buffer;
    pans::detail::SmallStreamBuffer<pans::detail::LOG_MESSAGE_INLINE_CAPACITY> stream_buffer(buffer);

    std::ostream os(&stream_buffer);
    os<<message;
    return Observe(buffer.view());
}

std::uint64_t WriteWithStringStream(std::string_view message){//标准库的  //todo :  
    std::stringstream stream;
    stream<<message;
    return Observe(stream.view());
}

template<typename Operation>
std::chrono::nanoseconds BenchMarkWrites(Operation && operation){  // 这里传的是 lambda , 匿名函数 ，就需要右值来接
    std::uint64_t checksum = 0;

    const auto begin = std::chrono::steady_clock::now();

    for(std::uint64_t iteration = 0; iteration < g_target; ++iteration)
    {
        checksum += operation();
    }
    const auto end = std::chrono::steady_clock::now();

    BENCHMARK_SINK.fetch_xor(checksum, std::memory_order_relaxed); // 因为这里不保留一下的话 ， 会把这个变量优化掉
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
}


template <typename Benchmark>
double Run(Benchmark&& benchmark)
{
    long double total_nanoseconds = 0.0L;
    for(int run = 0; run < RUNS; ++run)
    {
        total_nanoseconds += static_cast<long double>(benchmark().count());
    }
    return static_cast<double>(total_nanoseconds / RUNS);
}

void PrintResult(std::string_view name, double total_nanoseconds)
{
    std::cout << std::left << std::setw(30) << name << std::right
              << total_nanoseconds << " ns \ttotal, "
              << total_nanoseconds / static_cast<double>(g_target) << " ns/op\n";
}


int main(int argc, char** argv){
    if(argc != 2)
    {
        std::cerr << "Usage: ./test_log_buffer count\n";
        return 1;
    }
    g_target = std::stoull(argv[1]);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "operations: " << g_target << ", average of " << RUNS << " runs\n\n";
    constexpr std::array<std::size_t, 6> MESSAGE_SIZES = {50, 100, 200, 500, 1000, 2048};
    for(const std::size_t message_size : MESSAGE_SIZES)
    {
        const std::string message(message_size, 'x');
        const double small_stream_buffer_nanoseconds = Run([&]() {
            return BenchMarkWrites([&]() { return WriteWithSmallStreamBuffer(message); });
        });
        const double stringstream_nanoseconds = Run([&]() {
            return BenchMarkWrites([&]() { return WriteWithStringStream(message); }) ; 
        });

        std::cout << "message: " << message_size << " characters"  << '\n';
        PrintResult("SmallStreamBuffer", small_stream_buffer_nanoseconds);
        PrintResult("std::stringstream", stringstream_nanoseconds);
        std::cout << "speedup: " << stringstream_nanoseconds / small_stream_buffer_nanoseconds << "x\n\n";
    }

    return 0;
}