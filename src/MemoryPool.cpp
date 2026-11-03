#include"MemoryPool.h"

#include <cassert>

//请求大小直接换算成池下标：(size-1)/8 同时完成向上取整和减一
//8B -> 0, 16B -> 1, ..., 512B -> 63
static std::size_t poolIndex(std::size_t size)
{
    assert(size > 0 && size <= MAX_SLOT_SIZE);
    return (size - 1) / SLOT_BASE_SIZE;
}


void HashBucket::initMemoryPool()
{
    for(std::size_t i = 0 ; i < MEMORY_POOL_NUM ; i++)
    {
        getMemoryPool(i).init((i+1)*SLOT_BASE_SIZE);
    }
}


MemoryPool& HashBucket::getMemoryPool(std::size_t index)
{
    //函数内静态数组：初始化由语言保证线程安全
    static MemoryPool memoryPool[MEMORY_POOL_NUM];
    return memoryPool[index];
}


//每个线程每种档位一份缓存。绝大多数分配和释放只动这里，不进入中心池的锁。
HashBucket::ThreadCache* HashBucket::threadCache()
{
    thread_local ThreadCache cache[MEMORY_POOL_NUM];
    return cache;
}


//向中心池要一批槽，挂到本地链上
Slot* HashBucket::refill(std::size_t index,ThreadCache& cache)
{
    int got = 0;
    Slot* list = getMemoryPool(index).allocateBatch(REFILL_BATCH,got);

    if(list == nullptr || got == 0)
    {
        return nullptr;
    }

    cache.head = list;
    cache.count = got;
    return list;
}


//本地攒太多时把一半还回中心池，避免某个线程长期占着内存
void HashBucket::flushIfNeeded(std::size_t index,ThreadCache& cache)
{
    if(cache.count < FLUSH_THRESHOLD || cache.head == nullptr)
    {
        return;
    }

    const int move = cache.count / 2;

    //走到第 move 个节点，把它和后面断开
    Slot* tail = cache.head;

    for(int i = 1; i < move && tail->next != nullptr; ++i) tail = tail->next;

    Slot* rest = tail->next;
    tail->next = nullptr;

    getMemoryPool(index).deallocateList(cache.head);
    cache.head = rest;
    cache.count -= move;
}


void* HashBucket::useMemory(std::size_t size)
{
    if(size == 0)
    {
        return nullptr;
    }

    //大对象不进池：池只服务小对象，避免为偶发大请求浪费整块
    if(size > MAX_SLOT_SIZE)
    {
        return operator new(size);
    }

    const std::size_t index = poolIndex(size);
    ThreadCache& cache = threadCache()[index];

    if(cache.head == nullptr)
    {
        if(refill(index,cache) == nullptr)
        {
            return nullptr;
        }
    }

    Slot* slot = cache.head;
    cache.head = slot->next;
    --cache.count;
    return slot;
}


void HashBucket::freeMemory(void* ptr,std::size_t size)
{
    if(ptr == nullptr)
    {
        return;
    }

    if(size > MAX_SLOT_SIZE)
    {
        operator delete(ptr);
        return;
    }

    //size 为 0 时没有任何池能对应，直接忽略而不是算出下标 -1
    if(size == 0)
    {
        return;
    }

    const std::size_t index = poolIndex(size);
    ThreadCache& cache = threadCache()[index];

    Slot* slot = static_cast<Slot*>(ptr);
    slot->next = cache.head;
    cache.head = slot;
    ++cache.count;

    flushIfNeeded(index,cache);
}


MemoryPool::MemoryPool() = default;


//块头 8 字节存下一块的地址，槽区从块头之后、按槽大小对齐的位置开始
bool MemoryPool::allocateNewBlock()
{
    void* newBlock = operator new(BlockSize_);

    if(newBlock == nullptr)
    {
        return false;
    }

    reinterpret_cast<Slot*>(newBlock)->next = firstBlock_;
    firstBlock_ = reinterpret_cast<Slot*>(newBlock);

    char* body = reinterpret_cast<char*>(newBlock) + sizeof(Slot*);
    const std::size_t padding = padPointer(body,SlotSize_);

    curSlot_ = body + padding;
    blockEnd_ = reinterpret_cast<char*>(newBlock) + BlockSize_;
    return true;
}


//调用者已持有 Mutex
Slot* MemoryPool::popFreeLocked()
{
    Slot* slot = freeList_;

    if(slot != nullptr)
    {
        freeList_ = slot->next;
    }

    return slot;
}


//调用者已持有 Mutex：从当前块切一个新槽，块用完了就先申请一块
Slot* MemoryPool::carveLocked()
{
    if(curSlot_ == nullptr ||
       static_cast<std::size_t>(blockEnd_ - curSlot_) < SlotSize_)
    {
        if(!allocateNewBlock())
        {
            return nullptr;
        }
    }

    Slot* slot = reinterpret_cast<Slot*>(curSlot_);
    curSlot_ += SlotSize_;
    return slot;
}


Slot* MemoryPool::allocate()
{
    std::lock_guard<std::mutex> lock(Mutex);

    Slot* slot = popFreeLocked();
    return slot != nullptr ? slot : carveLocked();
}


//一次取一批：先掏空闲链，不够再从当前块切
Slot* MemoryPool::allocateBatch(int want,int& got)
{
    got = 0;

    if(want <= 0)
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(Mutex);

    Slot* head = nullptr;
    Slot* tail = nullptr;

    while(got < want)
    {
        Slot* slot = popFreeLocked();

        if(slot == nullptr)
        {
            slot = carveLocked();
        }

        if(slot == nullptr)
        {
            break;
        }

        slot->next = nullptr;

        if(tail == nullptr)
        {
            head = slot;
        }
        else
        {
            tail->next = slot;
        }

        tail = slot;
        ++got;
    }

    return head;
}


void MemoryPool::deallocateList(Slot* head)
{
    if(head == nullptr)
    {
        return;
    }

    //先把整条链的尾巴找出来，再一次挂到空闲链前面
    Slot* tail = head;

    while(tail->next != nullptr) tail = tail->next;

    std::lock_guard<std::mutex> lock(Mutex);
    tail->next = freeList_;
    freeList_ = head;
}


void MemoryPool::deallocate(void* ptr)
{
    if(ptr == nullptr)
    {
        return;
    }

    Slot* slot = static_cast<Slot*>(ptr);
    deallocateList(slot);
}


std::size_t MemoryPool::padPointer(char* p,std::size_t align)
{
    const std::size_t rem = reinterpret_cast<std::size_t>(p) % align;
    return rem == 0 ? 0 : align - rem;
}


bool MemoryPool::init(std::size_t size)
{
    if(size == 0 || size > MAX_SLOT_SIZE || size % SLOT_BASE_SIZE != 0)
    {
        return false;
    }

    SlotSize_ = size;
    return true;
}


//按块链逐块释放；槽本身不单独释放，它们都在块里
MemoryPool::~MemoryPool()
{
    Slot* block = firstBlock_;

    while(block != nullptr)
    {
        Slot* next = block->next;
        operator delete(reinterpret_cast<void*>(block));
        block = next;
    }

    firstBlock_ = nullptr;
    curSlot_ = nullptr;
    blockEnd_ = nullptr;
    freeList_ = nullptr;
}
