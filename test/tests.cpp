#include"../include/MemoryPool.h"
#include<iostream>
#include<thread>
#include<vector>
#ifdef _WIN32
#include <windows.h>
#endif

//测试用例

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



void BenchmarkNew(int ntimes,int nworks,int rounds) //参数分别为 单轮申请创建次数，线程数和轮次
{
    std::vector<std::thread>vthread(nworks);    //创建线程池
    
    int total_costtime = 0;

    for(int i = 0 ; i < nworks ; i++)
    {
        vthread[i] = std::thread([&](){
            for(int k = 0 ; k < rounds ; k++)
            {
                size_t begin1 = clock();
                for(int j = 0 ; j < ntimes ; j++)
                {
                    P1* p1 = new P1;
                    delete p1;
                    P2* p2 = new P2;
                    delete p2;
                    P3* p3 = new P3;
                    delete p3;
                    P4* p4 = new P4;
                    delete p4;
                }
                size_t end1 = clock();

                total_costtime += end1-begin1;
            }
        });
    }

    for (auto& t : vthread)
	{
		t.join();
	}

    std::cout << "普通情况" << nworks << "个线程运行" << rounds <<"轮次每轮创建与销毁"<< ntimes <<"次总耗时" << total_costtime << "ms" <<std::endl;
}

void BenchmarkMemoryPool(int ntimes,int nworks,int rounds)
{
    std::vector<std::thread>vthread(nworks);

    int total_costtime = 0;

    for(int i = 0 ; i < nworks ; i++)
    {
        vthread[i] = std::thread([&](){
            for(int k = 0 ; k < rounds ; k++)
            {
                size_t begin1 = clock();
                for(int j = 0 ; j < ntimes ; j++)
                {
                    P1* p1 = newElement<P1>();
                    deleteElement<P1>(p1);

                    P2* p2 = newElement<P2>();
                    deleteElement<P2>(p2);

                    P3* p3 = newElement<P3>();
                    deleteElement<P3>(p3);

                    P4* p4 = newElement<P4>();
                    deleteElement<P4>(p4);
                }
                size_t end1 = clock();

                total_costtime += end1 - begin1;
            }
        });
    }

    for (auto& t : vthread)
	{
		t.join();
	}

    std::cout <<"内存池" << nworks << "个线程运行" << rounds <<"轮次每轮创建与销毁"<< ntimes <<"次总耗时" << total_costtime << "ms" << std::endl;
}





int main()
{
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // 控制台输出使用 UTF-8
    SetConsoleCP(CP_UTF8);       // 控制台输入使用 UTF-8
    #endif
    
    HashBucket::initMemoryPool();

    BenchmarkMemoryPool(100,1,10000);
    std::cout << "====================" << std::endl;
    std::cout << "====================" << std::endl;
    BenchmarkNew(100,1,10000);

    return 0;
}
