#ifndef ECS_EVENT_HPP
#define ECS_EVENT_HPP

#include <iostream>
#include <functional>
#include <condition_variable>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <cassert>
#include "HashFunctions.hpp"

namespace ECS{

namespace EVENT{

template<typename,typename>
struct EventHelper;

template<typename Register,typename Type>
struct EventHelper {
	using EventID = ecs_map::id_type;
	using Register_Type = Register;

	EventHelper(Register_Type&reg,const EventID id = ecs_map::type_hash<Type>()):regist{reg}, eventId{id} {}
	
	// on_event 用の接続関数。Candidate は関数ポインタやラムダなど。
	template<auto Candidate, typename... Args>
	EventHelper& on_event(Args&&... args) {
		// Registry 内で EventType 用のシグナルを取得（実装依存ですが例として）
		auto& callback = regist.template getCallbackList(eventId);
		callback.template append<Candidate>(args...);
		return *this;
	}

	// 発火関数
	template<typename... Args>
	void call(Args&&... args) {
		auto& callback = regist.template getCallbackList(eventId);
		callback(std::forward<Args>(args)...);
	}

private:
	Register_Type *regist;
	EventID eventId;
};

template <typename T, typename ...Args>
struct HasFunctionGetEvent
{
	template <typename C> static std::true_type test(decltype(C::getEvent(std::declval<Args>()...))*);
	template <typename C> static std::false_type test(...);

	enum { value = !!decltype(test<T>(0))() };
};

template <typename E>
struct DefaultGetEvent
{
	template <typename U, typename ...Args>
	static E getEvent(U&& e, Args && ...) {
		return e;
	}
};
template <typename T, typename Key, bool> struct SelectGetEvent { using Type = T; };
template <typename T, typename Key> struct SelectGetEvent<T, Key, false> { using Type = DefaultGetEvent<Key>; };

// Policies が void の場合のコンテナ型
template <typename EventDataType>
using DefaultContainerType = std::vector<std::unique_ptr<EventDataType>>;

// HasTemplateQueueList：渡された型Tが、テンプレートメンバQueueListを持つかどうかを判定する
template <typename T>
struct HasTemplateQueueList {
	template <typename C>
	static std::true_type test(typename C::template QueueList<int>*);
	template <typename C>
	static std::false_type test(...);

	// Policiesがvoidの場合、そもそもこの判定を行う必要はないので、valueはfalseとなる
	enum { value = !std::is_same<T, void>::value && !!decltype(test<T>(0))() };
};

// ③ BusyQueueSelector の前方宣言
template <typename Policies, typename EventDataType, bool>
struct ContainerSelector;

// BusyQueueSelector：Policies が void の場合は常に DefaultBusyQueueType を使い、
// Policies が void でない場合は、HasTemplateQueueList<T>::value の結果に応じて型を選択する。
template<typename Policies, typename EventDataType, bool>
struct ContainerSelector {
	static_assert(!std::is_same<Policies, void>::value, "Policies==void should match the specialization");
	using type = typename Policies::template QueueList<EventDataType>;
};

template<typename Policies, typename EventDataType>
struct ContainerSelector<Policies, EventDataType, false> {
	using type = DefaultContainerType<EventDataType>;
};

template <typename F, typename... Args>
struct CanInvoke : std::is_invocable_r<bool, F, Args...> {};

// 使いやすさのための変数テンプレート
template <typename F, typename... Args>
constexpr bool CanInvoke_v = CanInvoke<F, Args...>::value;

// dtor 関数の型
using DtorFunc = void (*)(void*);

// 共通デストラクタ：内部バッファ上の T 型に対して呼び出す
template <typename T>
void commonDtor(void* instance) {
	reinterpret_cast<T*>(instance)->~T();
}

template <typename T>
class BufferedItem {
public:
	using ValueType = T;

	explicit BufferedItem() noexcept : buffer(), dtor(nullptr) { }

	~BufferedItem() noexcept {
		if (dtor != nullptr) {
			try {
				clear();
			}
			catch (...) {
				std::cerr<< typeid(T).name() << " clear faild!!"<<std::endl;
			}
		}
	}

	// コピー禁止、ムーブ禁止
	BufferedItem(BufferedItem&&) = delete;
	BufferedItem(const BufferedItem&) = delete;
	BufferedItem& operator = (const BufferedItem&) = delete;

	// オブジェクトを構築して設定する
	void set(T&& item) {
		assert(dtor == nullptr);  // 既に構築済みならエラー
		new (buffer.data()) T(std::forward<T>(item));
		dtor = &commonDtor<T>;
	}

	// 内部オブジェクトへのアクセス
	T& get() {
		assert(dtor != nullptr);
		return *reinterpret_cast<T*>(buffer.data());
	}

	const T& get() const {
		assert(dtor != nullptr);
		return *reinterpret_cast<const T*>(buffer.data());
	}

	// 内部オブジェクトの破棄（状態リセット）
	void clear() {
		assert(dtor != nullptr);
		dtor(buffer.data());
		dtor = nullptr;
	}

	// 状態判定：dtor が nullptr なら空とみなす
	bool empty() const noexcept {
		return dtor == nullptr;
	}

private:
	using StorageType = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
	std::array<StorageType, 1> buffer;
	DtorFunc dtor;
};

template<typename T>
struct CounterLock{
	explicit CounterLock(T& object) : counter(object){
		++counter;
	}

	~CounterLock(){
		if(counter>0)--counter;
	}

	T &counter;
};

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

}//namespaece EVENT
}//namespace ECS

#endif // !ECS_EVENT_H

