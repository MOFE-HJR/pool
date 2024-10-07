#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

//ö�٣����������̳߳ع���ģʽ
enum class PoolMode_{
	MODE_FIXED,
	MODE_CACHED
};

//ģ��Any��ʵ��
class Any_{
public:
	Any_() = default;
	~Any_() = default;
	Any_(const Any_&) = delete;
	Any_& operator=(const Any_&) = delete;
	Any_(Any_&&) = default;
	Any_& operator=(Any_&&) = default;

	//�вι���
	template<typename T>
	Any_(T data):base_(std::make_unique<Derive_<T>>(data))
	{}

public:
	//����ת������Any_ת��Ϊ��������
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
		//�黯����������ʹ��������Թ����Լ�������
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


//ģ���ź���ʵ��
class Semaphore_{
public:
	Semaphore_(int limit = 0);
	~Semaphore_();

public:
	//�ź���++
	void wait();
	//�ź���--
	void post();

private:
	size_t limit_;
	std::mutex mtx_;
	std::condition_variable cond_;

	//���Linux�����������������ǲ��ͷ���Դ���±�������(��������)
	//Windows��vs2022����Ҫ���
	bool isRunning;
};


//Task_�������
//Result_�����ڽ���ThreadPool_::submitTask()�ķ���ֵ����
class Task_;
class Result_{
public:
	Result_() = default;
	~Result_() = default;

	Result_(std::shared_ptr<Task_> task , bool isVaild = true);

public:
	//���ش洢��Any_��������
	Any_ get();
	//����Any_���͵�����
	void setVal(Any_ any);

private:
	Any_ any_;						//�洢ThreadPool_::submitTask()����ֵ
	Semaphore_ sem_;				//�����ж��ύ�������Ƿ�ִ�����
	std::shared_ptr<Task_> task_;	//ָ���ύ������
	std::atomic_bool isVaild_;		//�ж��ύ�������Ƿ���Ч
};


//����Task_���û�������̳д���
class Task_{
public:
	Task_();
	~Task_() = default;

public:
	//����ִ��
	void exec();
	//���÷���ֵ����Result_::Result_()�е���
	void setResult(Result_* result);
	//�û���дrun()��ʵ���û���Ҫ�Ĺ���
	virtual Any_ run() = 0;

private:
	Result_* result_;	//ʵ��Result_��Task_�İ�
};


//�̳߳��ڵ��߳���
class Thread_{
public:
	using ThreadFuc = std::function<void(size_t)>;
	Thread_(ThreadFuc func);
	~Thread_();

public:
	//�߳�����
	void start();
	
	//��ȡ�̳߳��е�id_ֵ
	size_t getId() const;

private:
	ThreadFuc func_;			//��������
	static size_t generateId_;	//��������id_��ֵ
	size_t id_;					//��Ӧ�߳����̳߳����keyֵ
};


//�̳߳�
class ThreadPool_{
public:
	ThreadPool_();
	~ThreadPool_();
	ThreadPool_ (const ThreadPool_&) = delete;
	ThreadPool_& operator=(const ThreadPool_&) = delete;

	//�����̳߳�ʼֵ
	void setInitThradSize(size_t initThreadSize);
	//���������������
	void setTaskQueMax(size_t taskQueMax);
	//�����̳߳ع���ģʽ(fixed,cache)
	void setMode(PoolMode_ mode);
	//�����̳߳ش洢���߳�����
	void setThreadMax(size_t threadMax);
	
	//�ж��̳߳�����״̬
	bool isRunning();
	
	//����������ύ����
	Result_ submitTask(std::shared_ptr<Task_> task);

	//�����̳߳�
	void start(size_t iniThradSize = 0);

private:
	//�߳�ִ�к���
	void threadFunc(size_t id);

private:
	
	std::unordered_map<size_t , std::unique_ptr<Thread_>> threads_;	//�̳߳�
	size_t initThreadSize_;											//�̳߳�ʼ������
	std::atomic_uint idleThreadSize_;								//�����̸߳���
	size_t threadMax_;												//����߳���
	std::atomic_uint threadSize_;									//��ǰ�߳���

	std::queue<std::shared_ptr<Task_>> taskQue_;					//�������
	std::atomic_uint taskSize_;										//��������
	size_t taskQueMax_;												//��������

	std::mutex taskQueMtx_;											//���������
	std::condition_variable notFull_;								//������в���
	std::condition_variable notEmpty_;								//������в���
	std::condition_variable exitCond_;								//�̳߳�����

	PoolMode_ mode_;												//�̳߳�ģʽ
	std::atomic_bool isPoolRunning_;								//�߳��Ƿ�����

};

#endif

