#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

//枚举，用于设置线程池工作模式
enum class PoolMode_{
	MODE_FIXED,
	MODE_CACHED
};

//模仿Any类实现
class Any_{
public:
	Any_() = default;
	~Any_() = default;
	Any_(const Any_&) = delete;
	Any_& operator=(const Any_&) = delete;
	Any_(Any_&&) = default;
	Any_& operator=(Any_&&) = default;

	//有参构造
	template<typename T>
	Any_(T data):base_(std::make_unique<Derive_<T>>(data))
	{}

public:
	//类型转换，将Any_转换为具体类型
	template<typename T>
	T cast_(){
		Derive_<T>* son = dynamic_cast<Derive_<T>*>(base_.get());
		if( son == nullptr ){
			throw "type is not match!!!";
		}
		return son->getData();
	}

private:
	class Base_{
	public:
		//虚化父类析构，使得子类可以管理自己的析构
		virtual ~Base_() = default;
	};

	template<typename T>
	class Derive_ :public Base_{
	public:
		Derive_(T data)
			:data_(data)
		{}
	public:
		T getData(){
			return data_;
		}
	private:
		T data_;
	};

private:
	std::unique_ptr<Base_> base_;
};


//模仿信号量实现
class Semaphore_{
public:
	Semaphore_(int limit = 0);
	~Semaphore_();

public:
	//信号量++
	void wait();
	//信号量--
	void post();

private:
	size_t limit_;
	std::mutex mtx_;
	std::condition_variable cond_;

	//针对Linux下条件变量析构但是不释放资源导致报错问题(出现死锁)
	//Windows下vs2022不需要添加
	bool isRunning;
};


//Task_类的声明
//Result_类用于接受ThreadPool_::submitTask()的返回值类型
class Task_;
class Result_{
public:
	Result_() = default;
	~Result_() = default;

	Result_(std::shared_ptr<Task_> task , bool isVaild = true);

public:
	//返回存储的Any_类型数据
	Any_ get();
	//设置Any_类型的数据
	void setVal(Any_ any);

private:
	Any_ any_;						//存储ThreadPool_::submitTask()返回值
	Semaphore_ sem_;				//用于判断提交的任务是否执行完成
	std::shared_ptr<Task_> task_;	//指向提交的任务
	std::atomic_bool isVaild_;		//判断提交的任务是否有效
};


//基类Task_，用户任务类继承此类
class Task_{
public:
	Task_();
	~Task_() = default;

public:
	//任务执行
	void exec();
	//设置返回值，在Result_::Result_()中调用
	void setResult(Result_* result);
	//用户重写run()，实现用户想要的功能
	virtual Any_ run() = 0;

private:
	Result_* result_;	//实现Result_与Task_的绑定
};


//线程池内的线程类
class Thread_{
public:
	using ThreadFuc = std::function<void(size_t)>;
	Thread_(ThreadFuc func);
	~Thread_();

public:
	//线程启动
	void start();
	
	//获取线程持有的id_值
	size_t getId() const;

private:
	ThreadFuc func_;			//函数对象
	static size_t generateId_;	//用于设置id_的值
	size_t id_;					//对应线程在线程池里的key值
};


//线程池
class ThreadPool_{
public:
	ThreadPool_();
	~ThreadPool_();
	ThreadPool_ (const ThreadPool_&) = delete;
	ThreadPool_& operator=(const ThreadPool_&) = delete;

	//设置线程初始值
	void setInitThradSize(size_t initThreadSize);
	//设置任务队列上限
	void setTaskQueMax(size_t taskQueMax);
	//设置线程池工作模式(fixed,cache)
	void setMode(PoolMode_ mode);
	//设置线程池存储的线程上限
	void setThreadMax(size_t threadMax);
	
	//判断线程池运行状态
	bool isRunning();
	
	//向任务队列提交任务
	Result_ submitTask(std::shared_ptr<Task_> task);

	//开启线程池
	void start(size_t iniThradSize = 0);

private:
	//线程执行函数
	void threadFunc(size_t id);

private:
	
	std::unordered_map<size_t , std::unique_ptr<Thread_>> threads_;	//线程池
	size_t initThreadSize_;											//线程初始化个数
	std::atomic_uint idleThreadSize_;								//空闲线程个数
	size_t threadMax_;												//最大线程数
	std::atomic_uint threadSize_;									//当前线程数

	std::queue<std::shared_ptr<Task_>> taskQue_;					//任务队列
	std::atomic_uint taskSize_;										//任务数量
	size_t taskQueMax_;												//任务上限

	std::mutex taskQueMtx_;											//任务队列锁
	std::condition_variable notFull_;								//任务队列不满
	std::condition_variable notEmpty_;								//任务队列不空
	std::condition_variable exitCond_;								//线程池销毁

	PoolMode_ mode_;												//线程池模式
	std::atomic_bool isPoolRunning_;								//线程是否运行

};

#endif

