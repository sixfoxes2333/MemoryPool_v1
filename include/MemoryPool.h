#pragma once
#include <cstddef>
#include <mutex>
#include <new>
#include <utility>

//内存池总数量：8B..512B，每 8 字节一档
inline constexpr std::size_t MEMORY_POOL_NUM = 64;
//基础槽大小
inline constexpr std::size_t SLOT_BASE_SIZE = 8;
//超过这个大小就不走池，直接给 operator new
inline constexpr std::size_t MAX_SLOT_SIZE = 512;

//线程缓存一次向中心池要多少个槽
inline constexpr int REFILL_BATCH = 64;
//线程缓存攒到多少个就开始还一半回去，避免某个线程囤积
inline constexpr int FLUSH_THRESHOLD = 128;

//空闲槽之间用这 8 字节串成链表；槽被分配出去后它就是用户数据
struct Slot
{
    Slot* next;
};


//一种固定槽大小的内存池：整块申请，块内按槽切分，释放的槽挂回空闲链表
class MemoryPool
{
public:
    MemoryPool();
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    //size 必须是 8 的倍数且在 8..512 之间
    bool init(std::size_t size);

    //分配一个槽；失败返回 nullptr
    Slot* allocate();

    //把槽还给本池
    void deallocate(void* ptr);

    //一次取一批（want 个，实际拿到 got 个），返回链头；线程缓存用
    Slot* allocateBatch(int want,int& got);

    //把一整条链还回来
    void deallocateList(Slot* head);

private:
    //块头用前 8 字节存下一块的地址，所以槽从块头之后开始
    bool allocateNewBlock();

    //把 p 向前推到 align 的整数倍，返回需要跳过多少字节
    static std::size_t padPointer(char* p,std::size_t align);

    //下面两个要求调用者已经持有 Mutex
    Slot* popFreeLocked();
    Slot* carveLocked();

    std::size_t SlotSize_ = 0;     //本池的槽大小
    std::size_t BlockSize_ = 4096; //每次向系统申请的块大小

    Slot* firstBlock_ = nullptr; //块链头，析构时按链释放
    char* curSlot_ = nullptr;    //当前块内下一个可用位置
    char* blockEnd_ = nullptr;   //当前块的结尾（不含）
    Slot* freeList_ = nullptr;   //已释放、可复用的槽

    //无锁栈在这个项目里踩到过 ABA：指针值没变但内容变了，CAS 看不出异常，
    //结果同一个槽被发给两个线程。这里改成互斥，并把临界区压到几次指针操作。
    std::mutex Mutex;
};


//按大小分档：把请求转发到对应槽大小的池，并给每个线程留一份小缓存
class HashBucket
{
public:
    //第一次使用前必须调用一次（进程内只调一次）
    static void initMemoryPool();

    static void* useMemory(std::size_t size);
    static void freeMemory(void* ptr,std::size_t size);

private:
    static MemoryPool& getMemoryPool(std::size_t index);

    //每个线程每种档位一条本地空闲链：绝大多数分配/释放只动这里，不碰锁
    struct ThreadCache
    {
        Slot* head = nullptr;
        int count = 0;
    };

    static ThreadCache* threadCache();

    //本地缓存不够时向中心池要一批
    static Slot* refill(std::size_t index,ThreadCache& cache);

    //攒太多就还一半回去，保证别的线程也能用到
    static void flushIfNeeded(std::size_t index,ThreadCache& cache);
};


template<typename T, typename... Args>
T* newElement(Args&&... args)
{
    void* memory = HashBucket::useMemory(sizeof(T));

    if(memory == nullptr)
    {
        return nullptr;
    }

    return new(memory) T(std::forward<Args>(args)...);
}


template<typename T>
void deleteElement(T* p)
{
    if(p == nullptr)
    {
        return;
    }

    p->~T();
    HashBucket::freeMemory(static_cast<void*>(p),sizeof(T));
}
