#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include "threadpool.h"

using ulong = unsigned long long;

class MyTask :public Task_{
public:
	MyTask(int begin , int end)
		:begin_(begin)
		, end_(end)
	{}

	Any_ run(){
		ulong sum = 0;
		for( int i = begin_; i <= end_; ++i ){
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(5));
		return sum;
	}

private:
	int begin_;
	int end_;
};

int main(){

	//exmaple 1
#if 0
	{
		ThreadPool_ pool;
		pool.setMode(PoolMode_::MODE_CACHED);
		pool.start();


		Result_ res1 = pool.submitTask(std::make_shared<MyTask>(1 , 100000000));
		Result_ res2 = pool.submitTask(std::make_shared<MyTask>(100000001 , 200000000));
		Result_ res3 = pool.submitTask(std::make_shared<MyTask>(200000001 , 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001 , 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001 , 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001 , 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001 , 300000000));

		ulong sum1 = res1.get().cast_<ulong>();
		ulong sum2 = res2.get().cast_<ulong>();
		ulong sum3 = res3.get().cast_<ulong>();

		std::cout << (sum1 + sum2 + sum3) << std::endl;
	}
#endif


	//exmaple 2
#if 0

	{
		ThreadPool_ pool;
		pool.start();


		Result_ res1 = pool.submitTask(std::make_shared<MyTask>(1 , 100));
		//ulong sum1 = res1.get().cast_<ulong>();
		//std::cout << sum1 << std::endl;
	}

#endif

	std::cout << "main over" << std::endl;
	getchar();

	return 0;
}