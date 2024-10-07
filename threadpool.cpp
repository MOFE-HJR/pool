#include "threadpool.h"
#include <iostream>
#include <thread>
#include <chrono>


/////////////		Semaphore_实现
Semaphore_::Semaphore_(int limit)
	:limit_(limit)
	,isRunning(true)
{}

Semaphore_::~Semaphore_(){
	isRunning = false;
}

void Semaphore_::wait(){
	if( !isRunning )
		return;
	std::unique_lock<std::mutex> lock(mtx_);
	cond_.wait(lock , [&]()->bool{ return limit_ > 0; });
	limit_--;
}

void Semaphore_::post(){
	if( !isRunning )
		return;
	std::unique_lock<std::mutex> lock(mtx_);
	limit_++;
	cond_.notify_all();
}





////////////////////////		Result_实现
Result_::Result_(std::shared_ptr<Task_> task , bool isVaild)
	:task_(task)
	,isVaild_(isVaild)
{
	//实现task与result的绑定
	task_->setResult(this);
}

Any_ Result_::get(){
	if( !isVaild_ ){
		return "";
	}

	//阻塞等待任务执行完成
	sem_.wait();
	
	return std::move(any_);
}

void Result_::setVal(Any_ any){
	any_ = std::move(any);
	//唤起阻塞的sem_
	sem_.post();
}




///////////////////////			Task_实现

Task_::Task_()
	:result_(nullptr)
{}

void Task_::exec(){
	if( result_ != nullptr ){
		//设置run的返回值
		result_->setVal(run());
	}
}

void Task_::setResult(Result_* result){
	result_ = result;
}





///////////////////////			Thread_实现
size_t Thread_::generateId_ = 0;
Thread_::Thread_(ThreadFuc func)
	:func_(func)
	,id_(generateId_++)
{}

Thread_::~Thread_()
{}

void Thread_::start(){
	//创建线程并设置线程分离
	std::thread t(func_,id_);
	t.detach();
}

size_t Thread_::getId() const{
	return id_;
}




///////////////////////////		ThreadPool_实现
const size_t TASK_QUE_MAX = INT32_MAX;		//任务队列上限值
const size_t THREAD_MAX = 1024;				//最大线程数
const size_t THREAD_MAX_IDLE_TIME = 60;		//线程最大等待时间，单位：s
ThreadPool_::ThreadPool_()
	:initThreadSize_(std::thread::hardware_concurrency())
	,taskSize_(0)
	,taskQueMax_(TASK_QUE_MAX)
	,mode_(PoolMode_::MODE_FIXED)
	,isPoolRunning_(false)
	,idleThreadSize_(0)
	,threadMax_(THREAD_MAX)
	,threadSize_(0)
{}

ThreadPool_::~ThreadPool_(){
	//设置线程池运行状态
	isPoolRunning_ = false;
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//唤起所有因为任务队列为空而等待的线程，执行销毁命令
	notEmpty_.notify_all();
	//等待线程池内所有线程被销毁
	exitCond_.wait(lock , [&]()->bool{ return threads_.size() == 0; });

}

void ThreadPool_::setInitThradSize(size_t initThreadSize){
	if( isPoolRunning_ )
		return;
	initThreadSize_ = initThreadSize;
}

void ThreadPool_::setTaskQueMax(size_t taskQueMax){
	if( isPoolRunning_ )
		return;
	taskQueMax_ = taskQueMax;
}

void ThreadPool_::setMode(PoolMode_ mode){
	if( isPoolRunning_ )
		return;
	mode_ = mode;
}

void ThreadPool_::setThreadMax(size_t threadMax){
	if( isPoolRunning_ )
		return;
	if(mode_ == PoolMode_::MODE_CACHED )
		threadMax_ = threadMax;
}

bool ThreadPool_::isRunning(){
	return isPoolRunning_;
}

Result_ ThreadPool_::submitTask(std::shared_ptr<Task_> task){

	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//任务队列满了，且等待时间超过1s，判断任务提交失败
	if( !(notFull_.wait_for(lock , std::chrono::seconds(1)
			, [&]()->bool{ return taskQueMax_ > taskQue_.size(); }))){
		std::cout << "task queue is full,submit task fail" << std::endl;
		return Result_(std::move(task),false);
	}

	//任务入队，并通知线程消费
	taskQue_.emplace(task);
	taskSize_++;
	notEmpty_.notify_all();

	//cache模式下，可以扩充线程池
	if( mode_ == PoolMode_::MODE_CACHED 
		&& taskQue_.size() > idleThreadSize_
		&& threadSize_ < threadMax_){

		std::cout << "create new thread" << std::endl;

		//创建线程对象，利用bind将函数对象和参数进行绑定
		//std::placeholders::_1 占位参数
		auto ptr = std::make_unique<Thread_>(std::bind(&ThreadPool_::threadFunc
			, this , std::placeholders::_1));
		size_t tmpId = ptr->getId();
		//unique_ptr 禁用左值 因此利用move进行资源转移
		threads_.emplace(tmpId , std::move(ptr));
		threads_[tmpId]->start();
		
		threadSize_++;
		idleThreadSize_++;
	}

	return Result_(std::move(task));
}

void ThreadPool_::start(size_t iniThradSize){

	//线程池参数设置
	if( iniThradSize > 0 ){
		initThreadSize_ = iniThradSize;
	}
	threadSize_ = (unsigned int )initThreadSize_;
	isPoolRunning_ = true;
	
	//初始化线程池
	for( size_t i = 0; i < initThreadSize_; ++i ){
		auto ptr = std::make_unique<Thread_>(std::bind(&ThreadPool_::threadFunc
			, this , std::placeholders::_1));
		size_t tmpId = ptr->getId();
		threads_.emplace(tmpId , std::move(ptr));
	}

	//启动线程
	for( size_t i = 0; i < initThreadSize_; ++i ){
		threads_[i]->start();
		idleThreadSize_++;
	}
}

void ThreadPool_::threadFunc(size_t id){
	
	//记录最近1次线程执行时间
	auto lastTime = std::chrono::high_resolution_clock().now();

	while( 1 ){

		std::shared_ptr<Task_> task;
		//获取任务队列的锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		std::cout << std::this_thread::get_id() << "尝试获取任务" << std::endl;

		//任务队列为空
		while( taskQue_.size() == 0 ){

			//如果线程池停止，销毁自身
			if( !isPoolRunning_ ){
				threads_.erase(id);
				std::cout << "delete thread:" << std::this_thread::get_id() << std::endl;
				exitCond_.notify_all();
				return;
			}

			//如果线程池模式为cache，可销毁多余线程
			if( mode_ == PoolMode_::MODE_CACHED ){

				//等待超时每1s返回一次
				if( std::cv_status::timeout ==
					notEmpty_.wait_for(lock , std::chrono::seconds(1)) ){
					auto nowTime = std::chrono::high_resolution_clock::now();
					auto durTime = std::chrono::duration_cast<std::chrono::seconds>
						(nowTime - lastTime);

					//等待超过规定时间(默认60s)，执行销毁线程命令
					if( durTime.count() >= THREAD_MAX_IDLE_TIME
						&& threadSize_ > initThreadSize_ ){
						threads_.erase(id);
						threadSize_--;
						idleThreadSize_--;
						std::cout << "delete thread:" << std::this_thread::get_id() << std::endl;
						return;
					}
				}
			}

			//如果模式不为cache，一直等待
			else{
				notEmpty_.wait(lock);
			}

		}

		//任务队列不为空，线程池内线程接收任务
		std::cout << std::this_thread::get_id() << "获取任务成功" << std::endl;
		task = taskQue_.front();
		taskQue_.pop();
		taskSize_--;
		idleThreadSize_--;
		//释放队列锁并通知
		lock.unlock();
		if( taskQue_.size() > 0 ){
			notEmpty_.notify_all();
		}
		notFull_.notify_all();

		//执行任务
		if( task != nullptr ){
			task->exec();
		}
		idleThreadSize_++;
		//更新线程最近活动时间
		lastTime = std::chrono::high_resolution_clock().now();
	}
	
}
