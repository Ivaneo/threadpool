#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <memory>
#include <condition_variable>

template<class _T>
struct AData
{
	AData():ready(false){}
	bool ready;
	_T data;
};

class ThreadPool
{
public:

	typedef std::function<void()> fn_type;

	class Worker
	{    
	public:

		Worker()
			:enabled(true),fqueue()
			,thread(&Worker::thread_fn, this)
		{}

		~Worker()
		{
			enabled = false;
			cv.notify_one();	
			thread.join();
		}  

		void appendFn(fn_type fn)
		{
			std::unique_lock<std::mutex> locker(mutex);
			fqueue.push(fn);			
			cv.notify_one();
		}	

		size_t getTaskCount() 		
		{ 
			std::unique_lock<std::mutex> locker(mutex);
			return fqueue.size();		
		}

		bool   isEmpty() 			
		{ 
			std::unique_lock<std::mutex> locker(mutex);
			return fqueue.empty();	
		}

	private:
		
		bool					enabled;
		std::condition_variable cv;
		std::queue<fn_type>		fqueue;		
		std::mutex				mutex;		
		std::thread				thread;			

		void thread_fn()
		{
			while (enabled)
			{
				std::unique_lock<std::mutex> locker(mutex);
				// Ожидаем уведомления, и убедимся что это не ложное пробуждение
				// Поток должен проснуться если очередь не пустая либо он выключен
				cv.wait(locker, [&](){ return !fqueue.empty() || !enabled; });				
				while(!fqueue.empty())
				{
					fn_type fn = fqueue.front();					
					// Разблокируем мютекс перед вызовом функтора
					locker.unlock();
					fn();
					// Возвращаем блокировку снова перед вызовом fqueue.empty() 
					locker.lock();
					fqueue.pop();
				}				
			}
		}
	};

	typedef std::shared_ptr<Worker> worker_ptr;

	ThreadPool(size_t threads = 1)
	{
		if (threads==0)
			threads=1;
		for (size_t i=0; i<threads; i++)
		{
			worker_ptr pWorker(new Worker);
			_workers.push_back(pWorker);
		}
	}

	~ThreadPool() {}

	template<class _R, class _FN, class... _ARGS>
	std::shared_ptr<AData<_R>> runAsync(_FN _fn, _ARGS... _args)
	{
		std::function<_R()> rfn = std::bind(_fn,_args...);  
		std::shared_ptr<AData<_R>> pData(new AData<_R>());
		fn_type fn = [=]()
		{
			pData->data = rfn();
			pData->ready = true;
		};
		auto pWorker = getFreeWorker();
		pWorker->appendFn(fn);
		return pData;
	}

	template<class _FN, class... _ARGS>
	void runAsync(_FN _fn, _ARGS... _args)
	{
		auto pWorker = getFreeWorker();
		pWorker->appendFn(std::bind(_fn,_args...));
	}

private:

	worker_ptr getFreeWorker()
	{
		worker_ptr pWorker;
		size_t minTasks = UINT32_MAX;				
		for (auto &it : _workers)
		{
			if (it->isEmpty())
			{
				return it;
			}
			else if (minTasks > it->getTaskCount())
			{
				minTasks = it->getTaskCount();
				pWorker = it;
			}
		}
		return pWorker;
	}

	std::vector<worker_ptr> _workers; 
	

};

#endif /*_THREADPOOL_H_*/