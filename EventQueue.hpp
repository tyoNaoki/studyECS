#ifndef ECS_EVENTQUEUE_HPP
#define ECS_EVENTQUEUE_HPP

#include "OrderedQueueList.hpp"
#include "EventDispatcher.hpp"
#include <vector>

namespace ECS {

namespace EVENT {

// イベントキューの宣言
template<typename EventType, typename Prototype, typename Policies = void>
class EventQueueBase;

template<typename EventType, typename ReturnType, typename... Args, typename Policies>
class EventQueueBase<EventType, ReturnType(Args...), Policies> :
	public EventDispatcher<EventType, ReturnType(Args...), Policies> {
private:
	using super = EventDispatcher<EventType, ReturnType(Args...), Policies>;

	using CallbackType = typename super::CallbackType;
	using CallBackListType = typename super::CallBacks;
	using StoredArgsTuple = std::tuple<std::decay_t<Args>...>;

	// イベントデータ：EventType と、引数のタプル
	struct QueuedEvent {
		EventType event;
		std::tuple<std::decay_t<Args>...> arguments;

		// コンストラクタ
		QueuedEvent(EventType e, std::tuple<std::decay_t<Args>...> args)
			: event(e), arguments(std::move(args)) { }

		// 再利用時に状態更新する
		void update(EventType e, std::tuple<std::decay_t<Args>...>&& args) {
			event = e;
			arguments = std::move(args);
		}

		// 引数取得用ユーティリティ
		template <std::size_t Index>
		auto getArgument() const {
			return std::get<Index>(arguments);
		}
	};

	using RawEventData = QueuedEvent;
	using EventData = BufferedItem<RawEventData>;

	//スマートポインターでラップした EventData を連続領域で管理
	using QueueContainer = typename ContainerSelector<Policies, EventData, HasTemplateQueueList<Policies>::value>::type;

public:
	// 通知制御用 RAII クラス
	struct DisableQueueNotify {
		explicit DisableQueueNotify(EventQueueBase* queue) : m_queue(queue) {
			m_queue->disableNotifications();
		}
		~DisableQueueNotify() {
			m_queue->enableNotifications();
		}
		DisableQueueNotify(const DisableQueueNotify&) = delete;
		DisableQueueNotify& operator=(const DisableQueueNotify&) = delete;
	private:
		EventQueueBase* m_queue;
	};

	// enqueue: イベントと引数からイベントデータを生成して登録
	//引数のみ指定
	template<typename... A>
	typename std::enable_if<sizeof...(A) == sizeof...(Args), void>::type
		enqueue(A&&... args) {

		using GetEvent = typename SelectGetEvent<Policies, EventType, HasFunctionGetEvent<Policies, A...>::value>::Type;

		const EventType& event = GetEvent::getEvent(args...);

		auto convertedArgs = std::make_tuple(convertArg(std::forward<A>(args))...);

		// idleList からノードを取得。空の場合は新たに生成する
		std::unique_ptr<EventData> node;
		if (!idleList.empty()) {
			std::lock_guard<std::mutex> lock(idleMutex);
			node = std::move(idleList.front());
			idleList.erase(idleList.begin());
		}
		else {
			node = std::make_unique<EventData>();
		}

		// ノードに対してイベントデータを設定する
		node->set(RawEventData(event, std::move(convertedArgs)));

		std::lock_guard<std::mutex> lock(queueMutex);
		// 生成（または再利用）したノードを busyQueue に追加する
		busyQueue.push_back(std::move(node));

		if (doCanProcess()) { cv_.notify_one(); }
	}

	template<typename T, typename... A>
	typename std::enable_if<sizeof...(A) == sizeof...(Args), void>::type
		enqueue(T&& first, A&&... args) {

		using GetEvent = typename SelectGetEvent<Policies, EventType, HasFunctionGetEvent<Policies, T&&, A...>::value>::Type;

		const EventType& event = GetEvent::getEvent(std::forward<T>(first), args...);

		// 入力引数の変換（各引数の型変換を一括処理）
		auto convertedArgs = std::make_tuple(convertArg(std::forward<A>(args))...);

		// idleList からノードを取得。空の場合は新たに生成する
		std::unique_ptr<EventData> node;
		if (!idleList.empty()) {
			std::lock_guard<std::mutex> lock(idleMutex);
			node = std::move(idleList.front());
			idleList.erase(idleList.begin());
		}
		else {
			node = std::make_unique<EventData>();
		}

		// ノードに対してイベントデータを設定する
		node->set(RawEventData(event, std::move(convertedArgs)));

		std::lock_guard<std::mutex> lock(queueMutex);
		// 生成（または再利用）したノードを busyQueue に追加する
		busyQueue.push_back(std::move(node));

		if (doCanProcess()) { cv_.notify_one(); }
	}

	void clearEvents()
	{
		QueueContainer tempQueue;
		{
			std::lock_guard<std::mutex> queueLock(queueMutex);
			std::swap(busyQueue, tempQueue);
		}

		if (tempQueue.empty()) return;

		for (auto& node : tempQueue) {
			node->clear();
		}

		std::lock_guard<std::mutex> queueLock(idleMutex);
		idleList.insert(
			idleList.end(),
			std::make_move_iterator(tempQueue.begin()),
			std::make_move_iterator(tempQueue.end())
		);
	}

	bool emptyQueue()const {
		return busyQueue.empty() && queueEmptyCount == 0;
	}

	// キューが空でなくなるまで wait する
	void wait() {
		std::unique_lock<std::mutex> queueLock(queueMutex);
		cv_.wait(queueLock, [this]() { return doCanProcess(); });
	}

	template <class Rep, class Period>
	bool waitFor(const std::chrono::duration<Rep, Period>& duration) const
	{
		std::unique_lock<std::mutex> queueLock(queueMutex);
		return cv_.wait_for(queueLock, duration, [this]() -> bool {
			return doCanProcess();
			});
	}

	// busyQueue の先頭イベントの EventType を取得し、削除する
	EventType dequeue() {

		if (!emptyQueue()) {
			std::lock_guard<std::mutex> lock(queueMutex);
			auto event = busyQueue.front()->get().event;
			busyQueue.erase(busyQueue.begin());
			return event;
		}

		return EventType{};
	}

	void disableNotifications() { ++disableCount; }
	void enableNotifications() {
		if (disableCount > 0) { --disableCount; }
		if (doCanProcess()) { cv_.notify_one(); }
	}

	// busyQueueにあるイベントを一括実行、処理後に idleListに加える
	bool process() {

		if (busyQueue.empty())return false;

		QueueContainer tempQueue;
		CounterLock<decltype(queueEmptyCount)> counterLock(queueEmptyCount);
		{
			std::lock_guard<std::mutex> queuelock(queueMutex);
			std::swap(busyQueue, tempQueue);
		}

		if (tempQueue.empty())return false;

		for (auto& node : tempQueue) {
			processEvent(node->get());
			node->clear();
		}

		std::lock_guard<std::mutex> queuelock(idleMutex);
		idleList.insert(
			idleList.end(),
			std::make_move_iterator(tempQueue.begin()),
			std::make_move_iterator(tempQueue.end())
		);

		return true;
	}

	// busyQueueから最初のイベントを一つだけ実行し、処理後にidleList に加える
	bool processOne()
	{
		std::lock_guard<std::mutex> queuelock(queueMutex);

		if (busyQueue.empty()) return false;

		std::unique_ptr<EventData> node = std::move(busyQueue.front());
		busyQueue.erase(busyQueue.begin());

		processEvent(node->get());
		node->clear();

		std::lock_guard<std::mutex> queuelock(idleMutex);
		idleList.push_back(std::move(node));

		return true;
	}

	// processIf関数は、bool関数(predictor)の結果に基づいて、イベントキュー内のイベントを処理する。  
	// predictorがtrueを返した場合のみイベントを実行し、falseの場合はイベントをキューに保持する。
	// 1件以上のイベントが実行された場合、trueを返す
	template <typename Predictor>
	bool processIf(Predictor&& predictor)
	{
		// busyQueueにイベントがなければ、falseを返す
		if (busyQueue.empty()) return false;

		// 一時的に処理対象となるbusyQueueのコピー用コンテナ
		QueueContainer tempBusyQueue;
		// 処理済みイベントを移動させるための一時的なIdleQueue
		std::vector<std::unique_ptr<EventData>> tempIdleQueue;

		// キューの空状態を管理するカウンターのロック（スレッドセーフ性）
		CounterLock<decltype(queueEmptyCount)> counterLock(queueEmptyCount);

		// busyQueueとtempBusyQueueを交換し、排他的に一時的なキューに移行する
		{
			std::lock_guard<std::mutex> queuelock(queueMutex);
			std::swap(busyQueue, tempBusyQueue);
		}

		// 交換後、tempBusyQueueが空なら、処理すべきイベントはないのでfalseを返す
		if (tempBusyQueue.empty()) return false;

		for (auto it = tempBusyQueue.begin(); it != tempBusyQueue.end(); ) {
			if (doInvokeFuncWithQueuedEvent(predictor, (*it)->get())) {
				// predictorがtrueを返した場合、イベントを実際に処理する
				processEvent((*it)->get());
				// イベントの内部情報削除
				(*it)->clear();

				tempIdleQueue.push_back(std::move(*it)); // 所有権の移動
				it = tempBusyQueue.erase(it); // 削除し、イテレーターを更新
			}
			else {
				// predictorがfalseの場合は、イベントをキューに残し、次のイベントへ進む
				++it;
			}
		}

		// ループ処理後、tempBusyQueueに未処理のイベントが残っている場合、
		// それらを再びbusyQueueへ戻す
		if (!tempBusyQueue.empty()) {
			std::lock_guard<std::mutex> queuelock(queueMutex);
			busyQueue.insert(
				busyQueue.end(),
				std::make_move_iterator(tempBusyQueue.begin()),
				std::make_move_iterator(tempBusyQueue.end())
			);
		}

		// tempIdleQueueに処理済みのイベントがあれば、
		// idleListへ移動
		if (!tempIdleQueue.empty()) {
			std::lock_guard<std::mutex> queuelock(idleMutex);
			idleList.insert(
				idleList.end(),
				std::make_move_iterator(tempIdleQueue.begin()),
				std::make_move_iterator(tempIdleQueue.end())
			);

			// 1件以上のイベントが実行されたので、trueを返す
			return true;
		}

		// どのイベントも実行されなかった場合はfalseを返す
		return false;
	}

	// processUntil関数は、bool関数(predictor)の結果をチェックし、
	// predictorがtrueを返した時点で残りのイベント処理を中断する。
	//時間制限(メインループの処理タイムアウト,ギミック)などの条件でイベント処理を一時停止するために使用される。
	// 1件以上のイベントが実行された場合、trueを返す
	template <typename Predictor>
	bool processUntil(Predictor&& predictor)
	{
		// busyQueueが空の場合、falseを返す
		if (busyQueue.empty()) return false;

		// busyQueueのイベントを一時的に格納するコンテナ
		QueueContainer tempBusyQueue;
		// 処理済みのイベントを一時的に保持するIdleQueue
		std::vector<std::unique_ptr<EventData>> tempIdleQueue;

		// キュー空状態カウンターのロックを取得
		CounterLock<decltype(queueEmptyCount)> counterLock(queueEmptyCount);

		{
			// busyQueueの状態を保護するためロックを取得し、tempBusyQueueと内容を交換する
			std::lock_guard<std::mutex> queuelock(queueMutex);
			std::swap(busyQueue, tempBusyQueue);
		}

		if (tempBusyQueue.empty()) return false;

		for (auto it = tempBusyQueue.begin(); it != tempBusyQueue.end();) {

			if (doInvokeFuncWithQueuedEvent(predictor, (*it)->get())) {
				// predictorがtrueの場合、処理を中断し、以降のイベントは処理せずにbreakする
				break;
			}
			else {
				// predictorがfalseの場合、イベントを実行する
				processEvent((*it)->get());
				// イベントの状態をクリア
				(*it)->clear();

				tempIdleQueue.push_back(std::move(*it)); // 所有権移動
				it = tempBusyQueue.erase(it); // 要素削除とitrの更新
			}
		}

		// ループ後、tempBusyQueueに残った未処理のイベントがあれば、
		// busyQueueに戻す
		if (!tempBusyQueue.empty()) {
			std::lock_guard<std::mutex> queuelock(queueMutex);
			busyQueue.insert(
				busyQueue.end(),
				std::make_move_iterator(tempBusyQueue.begin()),
				std::make_move_iterator(tempBusyQueue.end())
			);
		}

		// tempIdleQueueに1件でも処理済みのイベントがあれば、
		// idleListへ移動する
		if (!tempIdleQueue.empty()) {
			std::lock_guard<std::mutex> queuelock(idleMutex);
			idleList.insert(
				idleList.end(),
				std::make_move_iterator(tempIdleQueue.begin()),
				std::make_move_iterator(tempIdleQueue.end())
			);

			// 1件以上のイベントが実行されているのでtrueを返す
			return true;
		}

		// どのイベントも実行されなかった場合はfalseを返す
		return false;
	}


private:
	void processEvent(RawEventData& item) {
		auto& [eventType, args] = item;
		auto it = super::listeners.find(eventType);
		if (it != super::listeners.end()) {
			auto& callbackList = it->second;
			std::apply([&](auto&&... unpackedArgs) {
				callbackList(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
				}, args);
		}
	}

	inline std::string castConvert(const char* arg) { return std::string(arg); }
	template<typename T>
	auto castConvert(T&& arg) -> std::decay_t<T> {
		return static_cast<std::decay_t<T>>(std::forward<T>(arg));
	}
	template<typename T>
	auto convertArg(T&& arg) {
		return castConvert(std::forward<T>(arg));
	}

	bool doCanProcess()const{
		return !emptyQueue() && disableCount <= 0;
	}

	template <typename F>
	bool doInvokeFuncWithQueuedEvent(F&& func,RawEventData& qe) {
		if constexpr (CanInvoke_v<F, Args...>) {
			// 引数付きで呼び出し、戻り値を取得
			return std::apply(std::forward<F>(func), qe.arguments);
		}
		else {
			// 引数無しで呼び出す（別の処理が必要ならここを変更）
			return std::forward<F>(func)();
		}
	}

private:
	QueueContainer busyQueue;                         // 処理待ちのイベント
	std::vector<std::unique_ptr<EventData>> idleList;   // 再利用ノード
	mutable std::mutex queueMutex;
	std::mutex idleMutex;
	std::condition_variable cv_;

	int disableCount = 0;
	int queueEmptyCount = 0;
};

}//namespace EVENT
}//namespace ECS
#endif