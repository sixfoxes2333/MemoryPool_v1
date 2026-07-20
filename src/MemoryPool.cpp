#include"../include/MemoryPool.h"
void HashBucket:: initMemoryPool()
{
    for(int i = 0 ; i < MEMORY_POOL_NUM ; i++)
    {
        getMemoryPool(i).init((i+1)*SLOT_BASE_SIZE);
    }
}

MemoryPool& HashBucket:: getMemoryPool(int index)
{
    static MemoryPool memoryPool[MEMORY_POOL_NUM];
    return memoryPool[index];
}

void MemoryPool:: init(int size)
{
    curSlot_ = nullptr;
    firstBlock_ = nullptr;
    freeList_ = nullptr;
    lastSlot_ = nullptr;
    SlotSize_ = size;
}

void* HashBucket:: useMemory(size_t size)
{
    if(size == 0)
    {
        return nullptr;
    }

    if(size > MAX_SLOT_SIZE)
    {
        return operator new(size);
    }

   return getMemoryPool(((size+7)/SLOT_BASE_SIZE)-1).allocate();
}

void MemoryPool:: allocateNewBlock()
{
    void* newBlock = operator new(Blocksize_);
    reinterpret_cast<Slot*>(newBlock)->next = firstBlock_;
    firstBlock_ = reinterpret_cast<Slot*>(newBlock);

    char* body = reinterpret_cast<char*>(newBlock)+sizeof(Slot*);
    size_t paddingSize = padPointer(body,SlotSize_);
    curSlot_ = reinterpret_cast<Slot*>(body+paddingSize);

    lastSlot_ = reinterpret_cast<Slot*>(reinterpret_cast<size_t>(newBlock)+Blocksize_-SlotSize_+1);
}

Slot* MemoryPool:: popFreeList()
{
    while(true)
    {
        Slot* oldHead = freeList_.load(std::memory_order_acquire);

        if(oldHead == nullptr)
        {
            return nullptr;
        }
        
        Slot* newHead = nullptr;

        newHead = oldHead->next.load(std::memory_order_relaxed);

        if(freeList_.compare_exchange_strong(oldHead,newHead,std::memory_order_acquire,std::memory_order_relaxed))
        {
            return oldHead;
        }
    }
}

Slot* MemoryPool:: allocate()
{
    Slot* slot = popFreeList();
    if(slot != nullptr)
    {
        return slot;
    }
    
    
    Slot* temp = nullptr;

    {
        std::lock_guard<std::mutex> lock(Mutex);
        if(curSlot_ >= lastSlot_)
        {
            allocateNewBlock();
        }

        
        temp = curSlot_;
        curSlot_ += SlotSize_ / sizeof(Slot);
        

    }

    return temp;
}


void HashBucket::freeMemory(void* ptr,size_t size)
{
    if(!ptr)
    {
        return;
    }

    if( size > MAX_SLOT_SIZE)
    {

        operator delete(ptr);
        return;
    }

    getMemoryPool(((size+7)/SLOT_BASE_SIZE)-1).deallocate(ptr);

}

void MemoryPool::deallocate(void* ptr)
{
    if(!ptr)
    {
        return;
    }

    Slot* slot = reinterpret_cast<Slot*>(ptr);
    pushFreeList(slot);
}

bool MemoryPool::pushFreeList(Slot* slot)
{
    while(true)
    {
        Slot* oldHead = freeList_.load(std::memory_order_relaxed);
        slot->next.store(oldHead,std::memory_order_relaxed);

        if(freeList_.compare_exchange_weak(oldHead,slot,std::memory_order_release,std::memory_order_relaxed))
        {
            return true;
        }

    }
}

MemoryPool::MemoryPool()
{
    Blocksize_ = 4096;
    SlotSize_ = 0;
    firstBlock_ = nullptr;
    curSlot_ = nullptr;
    freeList_ = nullptr;
    lastSlot_ = nullptr;
}

size_t MemoryPool::padPointer(char* p,size_t align)
{
    size_t rem = reinterpret_cast<size_t>(p)%align;
    if(rem == 0)
    {
        return 0;
    }
    else
    {
        return align-rem;
    }
}

MemoryPool::~MemoryPool()
{
    Slot* cur = firstBlock_;

    while(cur)
    {
        Slot* next = cur->next;

        operator delete(reinterpret_cast<void*>(cur));

        cur = next;
    }
}