#pragma once
#include<atomic>
#include<mutex>
#include<utility>
#include<new>
#include<cstddef>
#define MEMORY_POOL_NUM 64  //内存池总数量
#define SLOT_BASE_SIZE 8    //基础内存槽大小
#define MAX_SLOT_SIZE 512   //最大内存槽支持数量

struct Slot
{
    std::atomic<Slot*>next; //原子指针
};


class MemoryPool
{
public:
    void init(int size); //初始化参数
    Slot* allocate();  //对外分配一个槽
    void allocateNewBlock();  //申请新内存块
    Slot* popFreeList();    //取出队列中空闲的槽  即 无锁出队
    void deallocate(void* ptr);  //释放内存并退回空闲槽
    bool pushFreeList(Slot* slot);  //无锁入队 把空闲的槽加回列表
    size_t padPointer(char* p,size_t align);

    MemoryPool();
    ~MemoryPool();


private:
    int SlotSize_;  //槽大小
    Slot* firstBlock_;    //指向内存池管理的首个内存块
    Slot* curSlot_;   //指向未被使用过的槽
    std::atomic<Slot*> freeList_;  //指向空闲的槽 ( 使用后又被释放 )
    Slot* lastSlot_;  //最后能够存放内存的槽的标识 ( 超过了就要申请新的 )
    size_t Blocksize_;  //块大小

    std::mutex Mutex;
};




class HashBucket
{
public:

static void* useMemory(size_t size);   //传参为要申请的字节数 调用内存
static void initMemoryPool();//初始化内存池
static MemoryPool& getMemoryPool(int index);
static void freeMemory(void* ptr,size_t size);

};


template<typename T,typename ... Args>
T* newElement(Args&&... args)
{
    T* p = nullptr;

    p = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T)));

    if(p != nullptr)
    {
        new(p) T (std::forward<Args>(args)...);
    }

    return p;
}

template<typename T>
void deleteElement(T* p)
{
    if(p)
    {
        p->~T();

        HashBucket::freeMemory(reinterpret_cast<void*>(p),sizeof(T));
    }
}