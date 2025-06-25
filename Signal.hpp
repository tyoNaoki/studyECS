#ifndef ECS_SIGNAL_HPP
#define ECS_SIGNAL_HPP

#include "CallbackList.hpp"
#include <vector>
#include <shared_mutex>
#include <functional>
#include <cstdint>
#include "EventDispatcher.hpp"
#include <typeinfo>


namespace ECS {

namespace EVENT {

	// イベント引数の型消去ホルダーの基底クラス
	struct IEventArg {
		virtual ~IEventArg() = default;
		virtual const std::type_info& type() const = 0;
	};

	// 任意の型 T を保持する派生クラス
	template<typename T>
	struct EventArg : IEventArg {
		T value;
		explicit EventArg(T&& v) : value(std::forward<T>(v)) {}
		const std::type_info& type() const override { return typeid(T); }
	};

class Signal : private EventDispatcher<ecs_map::id_type,void(void*)>
{
	using Base = EventDispatcher<ecs_map::id_type, void(void*)>;
	using HashID = ecs_map::id_type;
	using RawArg = void*;
	using Event = std::pair<HashID, std::unique_ptr<IEventArg>>;

	using Wrapper = std::function<void(RawArg)>;

	template<class U>
	inline static constexpr HashID hash = ecs_map::type_hash<U>();

public:
	
	template<class F>
	void connect(F&& f) { 
		using ObjT = function_arg_t<F>;
		using RealT = std::decay_t<ObjT>;
		HashID id = hash<RealT>;

		Wrapper w = [fn = std::forward<F>(f)](RawArg raw) {
			auto eventHolder = static_cast<const EventArg<RealT>*>(raw);

			if constexpr (std::is_const_v<std::remove_reference_t<ObjT>>) {
				fn(eventHolder->value);
			}
			else {
				fn(const_cast<std::remove_const_t<RealT>&>(eventHolder->value));
			}
		};

		// listeners マップへの登録
		std::scoped_lock lk(mtx_);
		listeners[id].append(std::move(w));
	}

	template<typename T>
	void publish(T&& eventArg) {
		using Decayed = std::decay_t<T>;
		HashID id = hash<Decayed>;
		auto holder = std::make_unique<EventArg<Decayed>>(std::forward<T>(eventArg));
		// イベントキューへの格納（ここでは piecewise_construct を利用）
		queue_.emplace_back(
			std::piecewise_construct,
			std::forward_as_tuple(id),
			std::forward_as_tuple(std::move(holder))
		);
	}
	
	void dispatch() {
		std::vector<Event> tmp;
		{
			std::scoped_lock lk(mtx_);
			tmp.swap(queue_);
		}

		// キューの空状態を管理するカウンターのロック（スレッドセーフ性）
		CounterLock<decltype(queueEmptyCount)> counterLock(queueEmptyCount);

		for (auto& [id, holder] : tmp) {
			auto it = listeners.find(id);
			if (it == listeners.end()) continue;
			// holder.get() は IEventArg* として渡す
			it->second(holder.get());
		}
	}

	template<typename T>
	void dispatch(HashID id = hash<std::decay_t<T>>)
	{
		std::vector<Event> work;
	
		{
			std::lock_guard<std::mutex> lk(mtx_);
			work.swap(queue_);                           
		}
	
		auto keepPos = std::partition(                   // 該当 ID を後ろ側へ集める
			work.begin(), work.end(),
			[id](const Event& e) { return e.first != id; });
	
		if (auto li = this->listeners.find(id); li != this->listeners.end())
			for (auto it = keepPos; it != work.end(); ++it)
				li->second(it->second);
	
		// 先頭側 (非対象) をキューへ戻す
		if (work.begin() != keepPos) {
			std::lock_guard<std::mutex> lk(mtx_);
			
			queue_.insert(queue_.end(),
				std::make_move_iterator(work.begin()),
				std::make_move_iterator(keepPos));
		}
	}
	
	bool dispatchOne()
	{
		Event ev;
		{
			std::lock_guard<std::mutex> lk(mtx_);
			if (queue_.empty()) return false;  // 空なら何もしない
			ev = std::move(queue_.back());    
			queue_.pop_back();               
		}
	
		auto id = ev.first;                               // リスナ呼び出し
		auto it = this->listeners.find(id);
	
		if (it == this->listeners.end()){
			std::lock_guard<std::mutex> lk(mtx_);
			queue_.emplace_back(
				std::piecewise_construct,
				std::forward_as_tuple(ev.first),
				std::forward_as_tuple(std::move(ev.second))
			);
			return false;
		};
	
		it->second(ev.second.get());
		return true;
	}
	

	/*
	// dispatchIf関数は、bool関数(predictor)の結果に基づいて、イベントキュー内のイベントを処理する。  
	// predictorがtrueを返した場合のみイベントを実行し、falseの場合はイベントをキューに保持する。
	// 1件以上のイベントが実行された場合、trueを返す
	template <typename BorrowT,typename Predictor>
	bool dispatchIf(Predictor&& predictor)
	{
		if(queue_.empty())return false;
	
		std::vector<Event> tempQueue;
	
		// キューの空状態を管理するカウンターのロック（スレッドセーフ性）
		CounterLock<decltype(queueEmptyCount)> counterLock(queueEmptyCount);
	
		{
			std::lock_guard<std::mutex> lk(mtx_);
			std::swap(queue_, tempQueue);
		}
	
		if(tempQueue.empty()) return false;
	
		bool result = false;
		for (auto it = tempQueue.begin(); it != tempQueue.end(); ) {
			if (doInvokeFuncWithQueuedEvent<BorrowT>(predictor, it->second)) {
				// predictorがtrueを返した場合、イベントを実際に処理する
				auto id = it->first;
				auto listenerItr = this->listeners.find(id);
	
				if (listenerItr == this->listeners.end()){
					++it;
					continue;
				} 
	
				listenerItr->second(it->second);
	
				bool result = true;
	
				it = tempQueue.erase(it); // 削除し、イテレーターを更新
			}
			else {
				// predictorがfalseの場合は、イベントをキューに残し、次のイベントへ進む
				++it;
			}
		}
	
		if(!tempQueue.empty())
		{
			std::lock_guard<std::mutex> lk(mtx_);
			queue_.insert(
				queue_.end(),
				std::make_move_iterator(tempQueue.begin()),
				std::make_move_iterator(tempQueue.end())
			);
		}
	
		return result;
	}

	// dispatchUntil関数は、bool関数(predictor)の結果をチェックし、
	// predictorがtrueを返した時点で残りのイベント処理を中断する。
	//時間制限(メインループの処理タイムアウト,ギミック)などの条件でイベント処理を一時停止するために使用される。
	// 1件以上のイベントが実行された場合、trueを返す
	template<class Pred>
	bool dispatchUntil(Pred&& predictor)
	{
		if (queue_.empty())return false;
	
		std::vector<Event> tempQueue;
	
		// キューの空状態を管理するカウンターのロック（スレッドセーフ性）
		CounterLock<decltype(queueEmptyCount)> counterLock(queueEmptyCount);
	
		{
			std::lock_guard<std::mutex> lk(mtx_);
			std::swap(queue_, tempQueue);
		}
	
		if (tempQueue.empty()) return false;
	
		bool result = false;
		for (auto it = tempQueue.begin(); it != tempQueue.end(); ) {
			if (doInvokeFuncWithQueuedEvent(predictor, it->second)) {
				break;
			}
			else {
				// predictorがtrueを返した場合、イベントを実際に処理する
				auto id = it->first;
				auto listenerItr = this->listeners.find(id);
	
				if (listenerItr == this->listeners.end()) {
					++it;
					continue;
				}
	
				listenerItr->second(it->second);
	
				bool result = true;
	
				it = tempQueue.erase(it); // 削除し、イテレーターを更新
			}
		}
	
		if (!tempQueue.empty())
		{
			std::lock_guard<std::mutex> lk(mtx_);
			queue_.insert(
				queue_.end(),
				std::make_move_iterator(tempQueue.begin()),
				std::make_move_iterator(tempQueue.end())
			);
		}
	
		return result;
	}
	*/

	Base::removeListener;
	Base::hasAnyListener;
	Base::haveHandle;

	bool emptyQueue()const {
		return queue_.empty() && queueEmptyCount == 0;
	};

	void wait() {
		std::unique_lock<std::mutex> lk(mtx_);
		cv_.wait(lk, [this] { return !emptyQueue(); });
	}

	template<class Rep, class Period>
	bool waitFor(const std::chrono::duration<Rep, Period>& d)
	{
		std::unique_lock<std::mutex> lk(mtx_);
		return cv_.wait_for(lk, d, [this]() -> bool {
			return !emptyQueue();
			});
	}

	void clearEvents()
	{
		queue_.clear();
	}

	template<typename T>
	void clearEvent(HashID id = hash<std::decay_t<T>>) {
		// queue_ は std::vector<std::pair<HashID,RawArg>>
		std::unique_lock<std::mutex> lk(mtx_);
		
		queue_.erase(
			std::remove_if(
				queue_.begin(),
				queue_.end(),
				[id](auto& ev) { return ev.first == id; }
			),
			queue_.end()
		);
		
	}

private:
	/* -- RawArg → Borrow<T*> 変換 -------------------------------------- */
	template<class Want>
	static Want convertArg(RawArg raw) {
		using Elem = typename Want::value_type;
		using Ptr = typename Want::pointer;
		return { static_cast<Ptr>(
				   static_cast<Elem*>(raw)) };
	}
	
	// static_assert 用ダミー
	template<class> static constexpr bool always_false_v = false;

	/*
	template<typename BorrowT,typename F>
	bool doInvokeFuncWithQueuedEvent(F&& func, RawArg raw) {
		// 1) bool func(BorrowT) が呼べるなら
		if constexpr (std::is_invocable_r_v<bool, F, BorrowT>) {
			return std::invoke(
				std::forward<F>(func),
				convertArg<BorrowT>(raw) 
			);
		}
	
		return false;
		
		// 2) bool func(void*) が呼べるなら
		else if constexpr (std::is_invocable_r_v<bool, F, RawArg>) {
			return std::invoke(
				std::forward<F>(func),
				raw
			);
		}
		// 3) bool func() が呼べるなら
		else if constexpr (std::is_invocable_r_v<bool, F>) {
			return std::invoke(std::forward<F>(func));
		}
		else {
			static_assert(always_false_v<F>,
				"Predicate must be "
				"bool(Borrow<T>), bool(void*), or bool()");
		}
		
	}
	*/

private:
	std::vector<Event>queue_;
	mutable std::mutex mtx_;
	std::condition_variable cv_;
	
	int queueEmptyCount = 0;
};

}//namespace EVENT
}//namespace ECS

#endif
