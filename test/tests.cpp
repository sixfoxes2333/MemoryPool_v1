#include"MemoryPool.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

//测试对象：四种不同大小的类，覆盖几个不同的槽档位
class P1
{
public:
    int id_;
};

class P2
{
public:
    int id_[5];
};

class P3
{
public:
    int id_[10];
};

class P4
{
public:
    int id_[20];
};


namespace
{

//每个线程各自记自己的耗时，join 之后再汇总，避免多线程抢同一个累加变量
struct Timing
{
    std::vector<long long> per_thread_ns;

    long long total_ns() const
    {
        long long sum = 0;
        for(const auto value : per_thread_ns) sum += value;
        return sum;
    }
};

//固定工作量：每轮把四个类各创建销毁一次
constexpr int kObjectsPerRound = 4;

//优化器会把"分配后立刻释放"整对删掉，所以把两个操作放进不内联、带编译屏障的薄封装
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void* opaqueNew(std::size_t size)
{
    void* p = ::operator new(size);
#if defined(__GNUC__)
    asm volatile("" ::: "memory");
#endif
    return p;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void opaqueDelete(void* p)
{
#if defined(__GNUC__)
    asm volatile("" ::: "memory");
#endif
    ::operator delete(p);
}

template<typename Work>
Timing measure(int works,int rounds,int ntimes,Work work)
{
    Timing timing;
    timing.per_thread_ns.assign(static_cast<std::size_t>(works),0);

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(works));

    for(int i = 0; i < works; ++i)
    {
        threads.emplace_back([&,i]
        {
            const auto begin = std::chrono::steady_clock::now();

            for(int round = 0; round < rounds; ++round)
            {
                for(int n = 0; n < ntimes; ++n) work();
            }

            const auto end = std::chrono::steady_clock::now();
            timing.per_thread_ns[static_cast<std::size_t>(i)] =
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
        });
    }

    for(auto& thread : threads) thread.join();
    return timing;
}

void report(const char* name,const Timing& timing,int works,int rounds,int ntimes)
{
    const long long total = timing.total_ns();
    const long long operations =
        static_cast<long long>(works) * rounds * ntimes * kObjectsPerRound;

    std::printf("%s：%d 线程 × %d 轮 × 每轮 %d 次建删（共 %lld 次） 总耗时 %.3f s，平均 %.1f ns/次\n",
                name,works,rounds,ntimes,operations,
                static_cast<double>(total) / 1e9,
                static_cast<double>(total) / static_cast<double>(operations));
}

//对照组：直接 new/delete
void benchmarkNew(int ntimes,int works,int rounds)
{
    const auto timing = measure(works,rounds,ntimes,[]
    {
        opaqueDelete(opaqueNew(sizeof(P1)));
        opaqueDelete(opaqueNew(sizeof(P2)));
        opaqueDelete(opaqueNew(sizeof(P3)));
        opaqueDelete(opaqueNew(sizeof(P4)));
    });

    report("new/delete",timing,works,rounds,ntimes);
}

//实验组：走内存池
void benchmarkMemoryPool(int ntimes,int works,int rounds)
{
    const auto timing = measure(works,rounds,ntimes,[]
    {
        deleteElement(newElement<P1>());
        deleteElement(newElement<P2>());
        deleteElement(newElement<P3>());
        deleteElement(newElement<P4>());
    });

    report("memory pool",timing,works,rounds,ntimes);
}
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    HashBucket::initMemoryPool();

    //两种实现各跑两轮：第一轮预热，只报告第二轮，减少顺序带来的偏差
    benchmarkNew(100,1,1);
    benchmarkMemoryPool(100,1,1);

    std::printf("------------------------------\n");
    benchmarkNew(100,1,10000);
    benchmarkMemoryPool(100,1,10000);

    std::printf("------------------------------\n");
    benchmarkNew(100,8,10000);
    benchmarkMemoryPool(100,8,10000);

    return 0;
}
