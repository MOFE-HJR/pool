#include "threadpool.h"
#include <iostream>
#include <thread>
#include <chrono>


/////////////		Semaphore_ʵ��
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





////////////////////////		Result_ʵ��
Result_::Result_(std::shared_ptr<Task_> task , bool isVaild)
	:task_(task)
	,isVaild_(isVaild)
{
	//ʵ��task��result�İ�
	task_->setResult(this);
}

Any_ Result_::get(){
	if( !isVaild_ ){
		return "";
	}

	//�����ȴ�����ִ�����
	sem_.wait();
	
	return std::move(any_);
}

void Result_::setVal(Any_ any){
	any_ = std::move(any);
	//����������sem_
	sem_.post();
}




///////////////////////			Task_ʵ��

Task_::Task_()
	:result_(nullptr)
{}

void Task_::exec(){
	if( result_ != nullptr ){
		//����run�ķ���ֵ
		result_->setVal(run());
	}
}

void Task_::setResult(Result_* result){
	result_ = result;
}





///////////////////////			Thread_ʵ��
size_t Thread_::generateId_ = 0;
Thread_::Thread_(ThreadFuc func)
	:func_(func)
	,id_(generateId_++)
{}

Thread_::~Thread_()
{}

void Thread_::start(){
	//�����̲߳������̷߳���
	std::thread t(func_,id_);
	t.detach();
}

size_t Thread_::getId() const{
	return id_;
}




///////////////////////////		ThreadPool_ʵ��
const size_t TASK_QUE_MAX = INT32_MAX;		//�����������ֵ
const size_t THREAD_MAX = 1024;				//����߳���
const size_t THREAD_MAX_IDLE_TIME = 60;		//�߳����ȴ�ʱ�䣬��λ��s
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
	//�����̳߳�����״̬
	isPoolRunning_ = false;
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//����������Ϊ�������Ϊ�ն��ȴ����̣߳�ִ����������
	notEmpty_.notify_all();
	//�ȴ��̳߳��������̱߳�����
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

	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//����������ˣ��ҵȴ�ʱ�䳬��1s���ж������ύʧ��
	if( !(notFull_.wait_for(lock , std::chrono::seconds(1)
			, [&]()->bool{ return taskQueMax_ > taskQue_.size(); }))){
		std::cout << "task queue is full,submit task fail" << std::endl;
		return Result_(std::move(task),false);
	}

	//������ӣ���֪ͨ�߳�����
	taskQue_.emplace(task);
	taskSize_++;
	notEmpty_.notify_all();

	//cacheģʽ�£����������̳߳�
	if( mode_ == PoolMode_::MODE_CACHED 
		&& taskQue_.size() > idleThreadSize_
		&& threadSize_ < threadMax_){

		std::cout << "create new thread" << std::endl;

		//�����̶߳�������bind����������Ͳ������а�
		//std::placeholders::_1 ռλ����
		auto ptr = std::make_unique<Thread_>(std::bind(&ThreadPool_::threadFunc
			, this , std::placeholders::_1));
		size_t tmpId = ptr->getId();
		//unique_ptr ������ֵ �������move������Դת��
		threads_.emplace(tmpId , std::move(ptr));
		threads_[tmpId]->start();
		
		threadSize_++;
		idleThreadSize_++;
	}

	return Result_(std::move(task));
}

void ThreadPool_::start(size_t iniThradSize){

	//�̳߳ز�������
	if( iniThradSize > 0 ){
		initThreadSize_ = iniThradSize;
	}
	threadSize_ = (unsigned int )initThreadSize_;
	isPoolRunning_ = true;
	
	//��ʼ���̳߳�
	for( size_t i = 0; i < initThreadSize_; ++i ){
		auto ptr = std::make_unique<Thread_>(std::bind(&ThreadPool_::threadFunc
			, this , std::placeholders::_1));
		size_t tmpId = ptr->getId();
		threads_.emplace(tmpId , std::move(ptr));
	}

	//�����߳�
	for( size_t i = 0; i < initThreadSize_; ++i ){
		threads_[i]->start();
		idleThreadSize_++;
	}
}

void ThreadPool_::threadFunc(size_t id){
	
	//��¼���1���߳�ִ��ʱ��
	auto lastTime = std::chrono::high_resolution_clock().now();

	while( 1 ){

		std::shared_ptr<Task_> task;
		//��ȡ������е���
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		std::cout << std::this_thread::get_id() << "���Ի�ȡ����" << std::endl;

		//�������Ϊ��
		while( taskQue_.size() == 0 ){

			//����̳߳�ֹͣ����������
			if( !isPoolRunning_ ){
				threads_.erase(id);
				std::cout << "delete thread:" << std::this_thread::get_id() << std::endl;
				exitCond_.notify_all();
				return;
			}

			//����̳߳�ģʽΪcache�������ٶ����߳�
			if( mode_ == PoolMode_::MODE_CACHED ){

				//�ȴ���ʱÿ1s����һ��
				if( std::cv_status::timeout ==
					notEmpty_.wait_for(lock , std::chrono::seconds(1)) ){
					auto nowTime = std::chrono::high_resolution_clock::now();
					auto durTime = std::chrono::duration_cast<std::chrono::seconds>
						(nowTime - lastTime);

					//�ȴ������涨ʱ��(Ĭ��60s)��ִ�������߳�����
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

			//���ģʽ��Ϊcache��һֱ�ȴ�
			else{
				notEmpty_.wait(lock);
			}

		}

		//������в�Ϊ�գ��̳߳����߳̽�������
		std::cout << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;
		task = taskQue_.front();
		taskQue_.pop();
		taskSize_--;
		idleThreadSize_--;
		//�ͷŶ�������֪ͨ
		lock.unlock();
		if( taskQue_.size() > 0 ){
			notEmpty_.notify_all();
		}
		notFull_.notify_all();

		//ִ������
		if( task != nullptr ){
			task->exec();
		}
		idleThreadSize_++;
		//�����߳�����ʱ��
		lastTime = std::chrono::high_resolution_clock().now();
	}
	
}
