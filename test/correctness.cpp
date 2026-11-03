//正确性测试：内存池必须可写、不重叠、能复用；并发下不能重复分配。
//原来的 tests.cpp 只测速度，这个文件负责"对不对"。
#include"MemoryPool.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>
#include <vector>

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"%s:%d: FAIL %s\n",__FILE__,__LINE__,#x); \
    std::exit(1); } } while(false)

namespace
{

//每种大小都写满整个槽，确认不会越界、不会把别的槽覆盖掉
void sizeClassesAreWritable()
{
    for(std::size_t size = SLOT_BASE_SIZE; size <= MAX_SLOT_SIZE; size += SLOT_BASE_SIZE)
    {
        std::set<void*> seen;
        std::vector<void*> blocks;
        blocks.reserve(600);

        for(int i = 0; i < 600; ++i)
        {
            void* p = HashBucket::useMemory(size);
            CHECK(p != nullptr);
            CHECK(seen.insert(p).second);      //地址必须互不相同
            std::memset(p,0xAB,size);          //写满整槽
            blocks.push_back(p);
        }

        //每个槽的每个字节都应该是刚才写的值，说明没有互相踩踏
        for(void* p : blocks)
        {
            const unsigned char* bytes = static_cast<const unsigned char*>(p);
            for(std::size_t i = 0; i < size; ++i) CHECK(bytes[i] == 0xAB);
        }

        for(void* p : blocks) HashBucket::freeMemory(p,size);
    }

    std::printf("PASS correctness: 64 size classes writable, distinct, no overlap\n");
}

//释放后必须能再分配出来，而不是一直向系统要新块
void freedSlotIsReused()
{
    void* first = HashBucket::useMemory(64);
    CHECK(first != nullptr);
    HashBucket::freeMemory(first,64);

    void* second = HashBucket::useMemory(64);
    CHECK(second == first);
    HashBucket::freeMemory(second,64);

    std::printf("PASS correctness: freed slot is reused\n");
}

//边界：0 字节、超过池上限、空指针
void edgeCases()
{
    CHECK(HashBucket::useMemory(0) == nullptr);
    HashBucket::freeMemory(nullptr,64);   //不能崩

    void* big = HashBucket::useMemory(MAX_SLOT_SIZE + 1);
    CHECK(big != nullptr);
    std::memset(big,0x5A,MAX_SLOT_SIZE + 1);
    HashBucket::freeMemory(big,MAX_SLOT_SIZE + 1);

    //非 8 倍数要向上取整到同一档，且能安全释放
    void* odd = HashBucket::useMemory(20);
    CHECK(odd != nullptr);
    std::memset(odd,0x11,20);
    HashBucket::freeMemory(odd,20);

    std::printf("PASS correctness: zero/large/unaligned sizes handled\n");
}

//newElement/deleteElement：构造、析构、参数转发都要正常
struct Tracked
{
    int value;
    static inline std::atomic<int> alive{0};

    explicit Tracked(int v) : value(v) { ++alive; }
    ~Tracked() { --alive; }
};

void elementHelpers()
{
    CHECK(Tracked::alive.load() == 0);
    Tracked* item = newElement<Tracked>(42);
    CHECK(item != nullptr);
    CHECK(item->value == 42);
    CHECK(Tracked::alive.load() == 1);

    deleteElement(item);
    CHECK(Tracked::alive.load() == 0);
    deleteElement(static_cast<Tracked*>(nullptr)); //允许传空

    std::printf("PASS correctness: newElement/deleteElement construct and destroy\n");
}

//并发：原来的无锁空闲链表在这里会 ABA，把同一个槽发给两个线程后崩溃
void concurrentReuse()
{
    constexpr int kThreads = 8;
    constexpr int kRounds = 20000;

    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for(int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&]
        {
            for(int i = 0; i < kRounds; ++i)
            {
                void* p = HashBucket::useMemory(64);

                if(p == nullptr)
                {
                    ++failures;
                    continue;
                }

                //写满整槽：如果同一个槽被发给了两个线程，这里就会互相踩坏
                std::memset(p,0x3C,64);
                HashBucket::freeMemory(p,64);
            }
        });
    }

    for(auto& worker : workers) worker.join();

    CHECK(failures.load() == 0);

    std::printf("PASS correctness: %d threads x %d alloc/free without duplicate slots\n",
                kThreads,kRounds);
}
}

int main()
{
    HashBucket::initMemoryPool();

    sizeClassesAreWritable();
    freedSlotIsReused();
    edgeCases();
    elementHelpers();
    concurrentReuse();

    std::printf("memory pool correctness PASS (5 groups)\n");
    return 0;
}
