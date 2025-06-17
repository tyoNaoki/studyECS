#ifndef ECS_BORROWDISPATCHER_HPP
#define ECS_BORROWDISPATCHER_HPP

#include "CallbackList.hpp"
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <cstdint>
#include "EventDispatcher.hpp"

namespace ECS {

namespace EVENT {

template<class,bool>
struct Borrow;

template<class T,bool Mut = false>
struct Borrow {
	using value_type = T;
	using pointer = std::conditional_t<Mut,T*,const T*>;
	pointer ptr{ nullptr };

#ifdef DEBUG
	~Borrow() { assert(ptr && "dangling Borrow"); }
#else
	~Borrow() = default;
#endif

	explicit operator bool() const noexcept { return ptr; }
	auto  operator->() const noexcept { return ptr; }
	auto& operator*()  const noexcept { return *ptr; }
};

template<typename U>
struct ensure_borrow {
	static_assert(
		sizeof(U) == 0,
		"ensure_delegate<U> must be instantiated only for T* or Delegate<T> types"
		);
};

// 2) Specialization for bare pointers (T* and const T*)
template<typename P>
struct ensure_borrow<P*> {
	// strip cv off P, then Delegate<P0>
	using P0 = std::remove_cv_t<P>;
	using type = Borrow<P0>;
};

template<typename P>
struct ensure_borrow<const P*> {
	using P0 = std::remove_cv_t<P>;
	using type = Borrow<const P0>;
};

// 3) Specialization for an existing Delegate<T>
template<typename P,bool Mut>
struct ensure_borrow<Borrow<P,Mut>> {
	using type = Borrow<P>;
};

template<class U> using ensure_borrow_t = typename ensure_borrow<U>::type;

// ラムダの引数型を抜き出すメタ関数
template<class> struct function_argument;
template<class R, class Arg, class...Rest>
struct function_argument<R(Arg, Rest...)> { using type = Arg; };
template<class R, class C, class Arg, class...Rest>
struct function_argument<R(C::*)(Arg, Rest...) const> { using type = Arg; };
template<class F>
struct function_argument : function_argument<decltype(&F::operator())> {};

template<class F>
using function_arg_t = typename function_argument<decltype(&F::operator())>::type;

class Signal : private EventDispatcher<ecs_map::id_type, void(void*)>
{
	using Base = EventDispatcher<ecs_map::id_type, void(void*)>;
	using HashID = ecs_map::id_type;
	using RawArg = void*;
	using Wrapper = std::function<void(RawArg)>;
	using Event = std::pair<HashID, RawArg>;

	// ハッシュは常に “Delegate 化した型” に対して
	template<class U>

	inline static constexpr HashID hash = ecs_map::type_hash<ensure_borrow_t<U>>();

public:

	template<class F>
	void connect(F&&f) {
		

		using ObjT = function_arg_t<F>;
		HashID id = hash<ObjT>;
		
		// 2) ポインタ型から Borrow<T,Mut> 型を決定
		using P0 = std::remove_pointer_t<ObjT>;  // T or const T
		constexpr bool Mut = !std::is_const_v<P0>;
		static_assert(Mut,"is const");
		using U = std::remove_cv_t<P0>;
		using B = Borrow<U, true>;

		// 3) その Borrow<T> 型でハッシュを求め
		
		/*

		// 4) 登録用 Wrapper（常に Borrow<T> を渡す）
		Wrapper w = [fn = std::forward<F>(fn)](RawArg raw){
			auto* p = reinterpret_cast<typename B::pointer>(raw);
			fn(B{ p });
		};

		std::scoped_lock lk(mtx_);
		listeners[id].append(std::move(w));
		*/
	}


	// 2-A) publish(T* or const T*)
	template<class U>
	void publish(U* ptr) {
		using D = ensure_borrow_t<U*>;
		enqueue(hash<U*>, reinterpret_cast<RawArg>(ptr));
	}

	// 3) 全イベント dispatch
	void dispatch() {
		std::vector<std::pair<HashID, RawArg>> tmp;
		{
			std::scoped_lock lk(mtx_);
			tmp.swap(queue_);
		}
		for (auto& [id, raw] : tmp) {
			if (auto it = this->listeners.find(id);
				it != this->listeners.end())
				it->second(raw);
		}
	}

	template<typename T>
	void dispatch(HashID id = hash<T>)
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
			if (queue_.empty()) return false;
			ev = queue_.back();
			queue_.pop_back();
		}
	
		auto id = ev.first;                               // リスナ呼び出し
		auto it = this->listeners.find(id);
	
		if (it == this->listeners.end()){
			queue_.emplace_back(ev.first, ev.second);
			return false;
		};
	
		it->second(ev.second);
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
	void clearEvent(HashID id = hash<T>) {
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

using Base::hasAnyListener;
using Base::haveHandle;
using Base::removeListener;

private:
	/* -- RawArg → Borrow<T*> 変換 -------------------------------------- */
	template<class Want>
	static Want convertArg(RawArg raw) {
		using Elem = typename Want::value_type;
		using Ptr = typename Want::pointer;
		return { static_cast<Ptr>(
				   static_cast<Elem*>(raw)) };
	}
	
	void enqueue(HashID id, RawArg raw) {
		std::scoped_lock lk(mtx_);
		queue_.emplace_back(id, raw);
	
		if (!emptyQueue()) { cv_.notify_one(); }
	}

	// static_assert 用ダミー
	template<class> static constexpr bool always_false_v = false;

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
		/*
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
		*/
	}

private:
	std::vector<Event>queue_;
	mutable std::mutex mtx_;
	std::condition_variable cv_;
	
	int queueEmptyCount = 0;
};



}//namespace EVENT
}//namespace ECS

#endif
