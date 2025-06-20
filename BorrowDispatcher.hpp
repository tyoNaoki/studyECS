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

	// 型消去のための基底クラス
	struct BorrowBase {
		virtual ~BorrowBase() = default;
	};

	// もともとの Borrow を継承させる
	template<class T, bool Mut>
	struct Borrow : BorrowBase {
		using value_type = T;
		using pointer = std::conditional_t<Mut, T*, const T*>;
		pointer ptr{ nullptr };

#ifdef DEBUG
		~Borrow() { assert(ptr && "dangling Borrow"); }
#else
		~Borrow() = default;
#endif

		explicit operator bool() const noexcept { return ptr; }
		auto operator->() const noexcept { return ptr; }
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

template<typename T, typename = void>
inline constexpr bool has_get_v = false;

template<typename T>
inline constexpr bool has_get_v<T,
	std::void_t<decltype(std::declval<T>().get())>
> = std::is_pointer_v<decltype(std::declval<T>().get())>;

class Signal : private EventDispatcher<ecs_map::id_type,void(void*)>
{
	using Base = EventDispatcher<ecs_map::id_type, void(void*)>;
	using HashID = ecs_map::id_type;
	using RawArg = void*;
	using Event = std::pair<HashID, std::unique_ptr<BorrowBase>>;
	using Wrapper = std::function<void(RawArg)>;
	//using Event = std::pair<HashID, RawArg>;

	// ハッシュは常に “Delegate 化した型” に対して
	//template<class U>
	//inline static constexpr HashID hash = ecs_map::type_hash<ensure_borrow_t<U>>();

	template<class U>
	inline static constexpr HashID hash = ecs_map::type_hash<U>();

public:
	template<class F>
	void connect(F&& f) {
		using ObjT = function_arg_t<F>; 
		HashID id = hash<ObjT>;
		using P0 = std::remove_pointer_t<ObjT>;  // T or const T
		constexpr bool Mut = !std::is_const_v<P0>;
		using U = std::remove_cv_t<P0>;
		using B = Borrow<U, Mut>;

		Wrapper w = [fn = std::forward<F>(f)](RawArg raw){
			if constexpr (std::is_pointer_v<ObjT>) {
				// ObjT が T* や const T* のケース
				fn(reinterpret_cast<ObjT>(raw));
			}
			else {
				// ObjT が T&, const T&, unique_ptr<T>&, const unique_ptr<T>& など
				using PT = std::remove_reference_t<ObjT>;
				// RawArg は “PT*” で enqueue している前提
				PT* ptr = reinterpret_cast<PT*>(raw);
				fn(*ptr);
			}
		};

		std::scoped_lock lk(mtx_);
		listeners[id].append(std::move(w));
	}

	/*
	template<
		typename P,
		typename = std::enable_if_t< has_get_v<std::decay_t<P>> >
	>
		void publish(P&& ptrLike) {
		// イベントキーは ObjT が受け取りたい正確な型（参照込み）に合わせる
		using KeyT = std::decay_t<P>&;
		enqueue(
			hash<KeyT>,
			// RawArg に渡すのはスマートポインタ自身へのポインタ
			reinterpret_cast<RawArg>(&ptrLike)
		);
	}

	template<
		class U,
		typename = std::enable_if_t< !has_get_v<std::remove_cv_t<U>> >
	>
		void publish(U* ptr)            // non-const T*
	{
		enqueue(hash<U*>, reinterpret_cast<RawArg>(ptr));
	}

	template<
		class U,
		typename = std::enable_if_t< !has_get_v<std::remove_cv_t<U>> >
	>
		void publish(const U* ptr)      // const T*
	{
		enqueue(hash<const U*>,
			reinterpret_cast<RawArg>(const_cast<U*>(ptr)));
	}
	*/

	template<class U>
	void publish(U* ptr) {
		// U* を受けるので Mut = true
		using B = Borrow<std::remove_cv_t<U>, /*Mut=*/true>;

		// 1) インスタンスを作って ptr をセット
		auto holder = std::make_unique<B>();
		holder->ptr = ptr;

		// 2) キー（connect で hash<ObjT> と同じになるよう ObjT=U*）
		HashID id = hash<U*>;

		// 3) queue_ に push
		queue_.emplace_back(id, std::move(holder));
	}
	
	
	template<class U>
	void publish(const U* ptr) {
		// const U* を受けるので Mut = false
		using B = Borrow<std::remove_cv_t<U>, /*Mut=*/false>;

		auto holder = std::make_unique<B>();
		holder->ptr = ptr;
		HashID id = hash<const U*>;
		queue_.emplace_back(id, std::move(holder));
	}

	// 同様に shared_ptr<T> や unique_ptr<T> 用に overload しても OK
	template<typename P>
	std::enable_if_t<has_get_v<std::decay_t<P>>, void>
		publish(P&& ptrLike) {
		// shared_ptr<T>&/&const なら ensure_borrow を使って同じように
		using T0 = std::remove_cv_t<std::remove_reference_t<P>>;
		using B = Borrow<T0, /*Mut=*/!std::is_const_v<T0>>;
		auto holder = std::make_unique<B>();
		holder->ptr = ptrLike.get();
		HashID id = hash<std::add_lvalue_reference_t<P>>;
		queue_.emplace_back(id, std::move(holder));
	}

	/*
	// 2-A) publish(T* or const T*)
	template<class U>
	void publish(const U* ptr) {
		enqueue(hash<U*>,static_cast<RawArg>(ptr));
	}
	*/

	void dispatch() {
		std::vector<Event> tmp;
		{
			std::scoped_lock lk(mtx_);
			tmp.swap(queue_);
		}
		for (auto& [id, holder] : tmp) {
			auto it = listeners.find(id);
			if (it == listeners.end()) continue;

			RawArg raw = holder.get();          // BorrowBase*
			it->second(raw);                     // connect 側のラッパーが static_cast して呼ぶ
		}
	}

	/*
	// 3) 全イベント dispatch
	void dispatch() {
		std::vector<std::pair<HashID, RawArg>> tmp;
		{
			std::scoped_lock lk(mtx_);
			tmp.swap(queue_);
		}
		for (auto& [id, raw] : tmp) {
			if (auto it = this->listeners.find(id);it != this->listeners.end())
				it->second(raw);
		}
	}
	*/

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
	/*
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
	*/

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
