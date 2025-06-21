#pragma once
#include "Debug.h"
#include "Scene.h"
#include "Component.h"
#include "World.h"
#include "SceneView.h"
#include <tuple>
#include <iostream>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <cassert>
#include "HopscotchHashMap.h"
#include "SparseHopscotch.h"
#include "unodreredDense.h"
#include <random>
#include "EventQueue.hpp"
#include "Signal.hpp"

// First let's define the event struct. e is the event type, priority determines the priority.
struct MyEvent
{
	int e;
	int priority;
};

// The comparison function object used by eventpp::OrderedQueueList.
// The function compares the event by priority.
struct MyCompare
{
	template <typename T>
	bool operator() (const T& a, const T& b) const {
		return a.template getArgument<0>().priority > b.template getArgument<0>().priority;
	}
};

// Define the EventQueue policy
struct MyPolicy
{
	template <typename Item>
	using QueueList = ECS::EVENT::OrderedQueueList<Item, MyCompare>;

	static int getEvent(const MyEvent& event) {
		return event.e;
	}
};

struct Position
{
	Position(float _x, float _y) :x(_x), y(_y) {};
	Position() = default;
	float x = 0;
	float y = 0;
};

struct  Velocity
{
	Velocity(float _x, float _y) :x(_x), y(_y) {};
	Velocity() = default;
	float x = 0;
	float y = 0;
};

struct TransformComponent {
	float _x, _y, _z;

	TransformComponent(float x = 0, float y = 0, float z = 0) : _x(x), _y(y), _z(z) {}
};

void test_create_group();
void hashMapBenchmarks();
void typeListTest();
void worldEventTest();
void testEventQueue();
void testCallbackList();
void testDispatch();
void test_wait_unblocks_on_event();
void test_waitFor_times_out_and_succeeds();

void test()
{
}

bool myCallback(const int a, const std::unique_ptr<int>& b) {
	return a < *b;
}

bool myFallback() {
	return false;
}

inline ECS::ecs_map::id_type hash_idApple() { return 1; }
inline ECS::ecs_map::id_type hash_idBanana() { return 2; }

void worldEventTest()
{
	struct Apple{
		int x = 5;
		int y = 0;
	};

	struct Banana {
		int x = 30;
		int y = 0;
		Banana(int x_, int y_) : x(x_), y(y_) {}
		Banana() = default;
	};

	struct Grape {
		int x = 30;
		int y = 0;
	};

	ECS::EVENT::Signal bus;                     // EventType = int
	
	
	// 読み取り専用のListener
	bus.connect([](const Apple* a) {
		std::cout << "const Apple x = " << a->x << "\n";
		});
	
	// 変更可能なListener
	bus.connect([](Apple* a) {
		a->x += 10;
		std::cout << "Apple*a->x = " << a->x << "\n";
		});

	// shared_ptrも素のポインタと同様に渡せる
	bus.connect([](const std::unique_ptr<const Banana>* s) {
		std::cout << "const std::unique_ptr<Banana>* s->x  = " << s->get()->x << "\n";
		});
	
	bus.connect([](std::unique_ptr<Banana>* s) {
		std::cout << "std::unique_ptr<Banana>& s->x  = " << s->get()->x << "\n";
		s->get()->x = 100;
		});
	
	bus.connect([](const std::shared_ptr<Grape>* s) {
		std::cout << "const std::shared_ptr<Grape>&s->x  = " << s->get()->x << "\n";
		});

	bus.connect([](std::shared_ptr<Grape>* s) {
		std::cout << "std::shared_ptr<Grape>&->x  = " << s->get()->x << "\n";
		s->get()->x += 50;
		});
	

	// 発行側：ポインタ or スマートポインタだけ書けば OK
	Apple a{ 42 };
	const Apple ca{ 99 };
	
	const std::unique_ptr<const Banana> cb = std::make_unique<Banana>(10,10);
	std::unique_ptr<Banana> b = std::make_unique<Banana>();
	std::shared_ptr<Grape> g = std::make_shared<Grape>();
	const std::shared_ptr<Grape> cg = std::make_shared<Grape>();

	bus.publish(&a);
	bus.publish(&ca);
	bus.publish(&cb);
	bus.publish(&b);
	bus.publish(&cg);
	bus.publish(&g);

	//Banana b{ 7 };
	//bus.publish(&b);   // Banana* リスナ呼び出し

	// フレーム末に一括ディスパッチ
	bus.dispatch();

	std::cout << "change unique_ptr<Banana>b->x = " << b->x << std::endl;
	std::cout << "change shared_ptr<Grapge>g->x = " << g->x << std::endl;

	//bus.dispatch();

	//std::cout << "banana y = " << b.y << '\n';   // 10

	// 発行後も a/b は依然 発行側が所有
	
}

/*
void test_wait_unblocks_on_event() {
	std::atomic<bool> unblocked{ false };
	struct Apple {
		int x = 5;
		int y = 0;
	};

	struct Banana {
		int x = 30;
		int y = 0;
	};

	ECS::EVENT::BorrowDispatcher bus;

	// 1) ワーカーはまず wait() でブロック
	std::thread worker([&] {
		bus.wait();
		unblocked = true;
		});

	// 2) 少し待ってブロッキング中であることを確認
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	assert(unblocked == false);

	// 3) イベントを publish & dispatch して wakeup させる
	Apple a{ 777 };
	//bus.publish(1, &a);
	bus.dispatch();

	// 4) worker が目覚めるまで少し待機
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	assert(unblocked == true);

	worker.join();
	std::cout << "test_wait_unblocks_on_event: OK\n";
}

void test_waitFor_times_out_and_succeeds() {
	using namespace std::chrono;
	struct Apple {
		int x = 5;
		int y = 0;
	};

	struct Banana {
		int x = 30;
		int y = 0;
	};
	Apple a{ 123 };
	ECS::EVENT::BorrowDispatcher bus;

	// 1) 空のままで waitFor がタイムアウトすること
	auto t0 = high_resolution_clock::now();
	bool got = false;
	got = bus.waitFor(milliseconds(200));
	auto dt = duration_cast<milliseconds>(high_resolution_clock::now() - t0);
	assert(got == false);
	assert(dt >= milliseconds(190));         // ほぼ200ms待った

	std::cout << "waitFor timeout: OK ( waited "
		<< dt.count() << " ms )\n";

	// 2) イベント到着後ならすぐ戻ること
	//    先に publish しておく
	//bus.publish(1, &a);

	t0 = high_resolution_clock::now();
	got = bus.waitFor(milliseconds(500));
	dt = duration_cast<milliseconds>(high_resolution_clock::now() - t0);
	assert(got == true);
	assert(dt < milliseconds(50));           // ほぼ即戻り

	// dispatch を呼ぶのを忘れずに
	bus.dispatch();

	std::cout << "waitFor wakeup: OK ( waited "
		<< dt.count() << " ms )\n";
}
*/

void testEventQueue()
{
	
	{
		ECS::EVENT::EventQueueBase<int, void(const std::string&, std::unique_ptr<int>&)> queue;

		queue.appendListener(3, [](const std::string& s, std::unique_ptr<int>& n) {
			std::cout << "Got event 3, s is " << s << " n is " << *n << std::endl;
			});
		// The listener prototype doesn't need to be exactly same as the dispatcher.
		// It would be find as long as the arguments is compatible with the dispatcher.
		queue.appendListener(5, [](std::string s, const std::unique_ptr<int>& n) {
			std::cout << "Got event 5, s is " << s << " n is " << *n << std::endl;
			});
		queue.appendListener(5, [](const std::string& s, std::unique_ptr<int>& n) {
			std::cout << "Got another event 5, s is " << s << " n is " << *n << std::endl;
			});

		// Enqueue the events, the first argument is always the event type.
		// The listeners are not triggered during enqueue.
		queue.enqueue(3, "Hello", std::unique_ptr<int>(new int(38)));
		queue.enqueue(5, "World", std::unique_ptr<int>(new int(58)));

		queue.processOne();
	}
	

	{
		ECS::EVENT::EventQueueBase<int, void(const int&, std::unique_ptr<int>&)> queue;

		queue.appendListener(3, [](const int s, std::unique_ptr<int>& n) {
			std::cout << "Got event 3, s is " << s << " n is " << *n << std::endl;
			});
		// The listener prototype doesn't need to be exactly same as the dispatcher.
		// It would be find as long as the arguments is compatible with the dispatcher.
		queue.appendListener(5, [](const int s, const std::unique_ptr<int>& n) {
			std::cout << "Got event 5, s is " << s << " n is " << *n << std::endl;
			});

		// Enqueue the events, the first argument is always the event type.
		// The listeners are not triggered during enqueue.
		queue.enqueue(3, 2, std::unique_ptr<int>(new int(5)));
		queue.enqueue(5, 12, std::unique_ptr<int>(new int(3)));
		
		queue.processIf([](const int a, const std::unique_ptr<int>& b){
			return a > *b;
		});
		
		queue.processIf(myCallback);

		queue.processIf(myFallback);

		queue.processUntil([]() {
			return false;
			});
	}
	
	
	{
	ECS::EVENT::EventQueueBase<int, void(const std::string&, std::unique_ptr<int>&)> queue;

	queue.appendListener(3, [](const std::string& s, std::unique_ptr<int>& n) {
		std::cout << "Got event 3, s is " << s << " n is " << *n << std::endl;
		});
	// The listener prototype doesn't need to be exactly same as the dispatcher.
	// It would be find as long as the arguments is compatible with the dispatcher.
	queue.appendListener(5, [](std::string s, const std::unique_ptr<int>& n) {
		std::cout << "Got event 5, s is " << s << " n is " << *n << std::endl;
		});
	queue.appendListener(5, [](const std::string& s, std::unique_ptr<int>& n) {
		std::cout << "Got another event 5, s is " << s << " n is " << *n << std::endl;
		});

	// Enqueue the events, the first argument is always the event type.
	// The listeners are not triggered during enqueue.
	queue.enqueue(3, "Hello", std::unique_ptr<int>(new int(38)));
	queue.enqueue(5, "World", std::unique_ptr<int>(new int(58)));

	// Process the event queue, dispatch all queued events.
	queue.process();

	std::cout << std::endl;
	}
	
	
	///////////////2
	
	
	{
	using EQ = ECS::EVENT::EventQueueBase<int, void(int)>;
	EQ queue2;

	constexpr int stopEvent = 1;
	constexpr int otherEvent = 2;
	
	// Start a thread to process the event queue.
	// All listeners are invoked in that thread.
	std::thread thread([stopEvent, otherEvent, &queue2]() {
		volatile bool shouldStop = false;
		queue2.appendListener(stopEvent, [&shouldStop](int) {
			shouldStop = true;
			});
		queue2.appendListener(otherEvent, [](const int index) {
			std::cout << "Got event, index is " << index << std::endl;
			});
		
		while (!shouldStop) {
			queue2.wait();

			queue2.process();
		};
		
	});
	
	// Enqueue an event from the main thread. After sleeping for 10 milliseconds,
	// the event should have be processed by the other thread.
	queue2.enqueue(otherEvent, 1);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	std::cout << "Should have triggered event with index = 1" << std::endl;

	queue2.enqueue(otherEvent, 2);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	std::cout << "Should have triggered event with index = 2" << std::endl;

	{
		// EventQueueBase::DisableQueueNotify is a RAII class that
		// disables waking up any waiting threads.
		// So no events should be triggered in this code block.
		// DisableQueueNotify is useful when adding lots of events at the same time
		// and only want to wake up the waiting threads after all events are added.
		EQ::DisableQueueNotify disableNotify(&queue2);

		queue2.enqueue(otherEvent, 10);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		std::cout << "Should NOT trigger event with index = 10" << std::endl;

		queue2.enqueue(otherEvent, 11);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		std::cout << "Should NOT trigger event with index = 11" << std::endl;
	}
	// The DisableQueueNotify object is destroyed here, and has resumed
	// waking up waiting threads.
	//So the events should be triggered.
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	std::cout << "Should have triggered events with index = 10 and 11" << std::endl;

	queue2.enqueue(stopEvent, 1);
	thread.join();
	}
	
	
	
	{
	std::cout << std::endl << "EventQueue tutorial 3, ordered queue" << std::endl;

	using EQ = ECS::EVENT::EventQueueBase<int, void(const MyEvent&), MyPolicy>;
	EQ queue3;

	queue3.appendListener(3, [](const MyEvent& event) {
		std::cout << "Get event " << event.e << "(should be 3)."
			<< " priority: " << event.priority << std::endl;
		});
	queue3.appendListener(5, [](const MyEvent& event) {
		std::cout << "Get event " << event.e << "(should be 5)."
			<< " priority: " << event.priority << std::endl;
		});
	queue3.appendListener(7, [](const MyEvent& event) {
		std::cout << "Get event " << event.e << "(should be 7)."
			<< " priority: " << event.priority << std::endl;
		});

	// Add an event, the first number 5 is the event type, the second number 100 is the priority.
	// After the queue processes, the events will be processed from higher priority to lower priority.
	
	queue3.enqueue(MyEvent{ 5, 100 });
	queue3.enqueue(MyEvent{ 5, 200 });
	queue3.enqueue(MyEvent{ 7, 300 });
	queue3.enqueue(MyEvent{ 7, 400 });
	queue3.enqueue(MyEvent{ 3, 500 });
	queue3.enqueue(MyEvent{ 3, 600 });
	
	queue3.process();
	}
	
	
	
	{

		// This is the definition of event types
		enum class EventType
		{
			// for MouseEvent
			mouse,

			// for KeyboardEvent
			keyboard,

			// for either MouseEvent or KeyboardEvent, it's used to demonstrate
			// how the listener detects event type dynamically
			input,

			// for ChangedEvent
			changed
		};

		// This is the base event. It has a getType function to return the actual event type.
		class Event
		{
		public:
			explicit Event(const EventType type) : type(type) {
			}

			virtual ~Event() {
			}

			EventType getType() const {
				return type;
			}

		private:
			EventType type;
		};

		class MouseEvent : public Event
		{
		public:
			MouseEvent(const int x, const int y)
				: Event(EventType::mouse), x(x), y(y)
			{
			}

			int getX() const { return x; }
			int getY() const { return y; }

		private:
			int x;
			int y;
		};

		class KeyboardEvent : public Event
		{
		public:
			explicit KeyboardEvent(const int key)
				: Event(EventType::keyboard), key(key)
			{
			}

			int getKey() const { return key; }

		private:
			int key;
		};

		class ChangedEvent : public Event
		{
		public:
			explicit ChangedEvent(const std::string& text)
				: Event(EventType::changed), text(text)
			{
			}

			std::string getText() const { return text; }

		private:
			std::string text;
		};

		// We will pass event as EventPointer, here it's std::shared_ptr<Event>.
		// It allows EventQueue to store the events in internal buffer without slicing the objects
		// in asynchronous API (EventQueue::enqueue and EventQueue::process, etc).
		// If we only use the synchronous API (EventDispatcher, or EventQueue::dispatch),
		// we can dispatch events as reference.
		using EventPointer = std::shared_ptr<Event>;

		// We are going to dispatch event objects directly without specify the event type explicitly,
		// so we need to define policy to let eventpp know how to get the event type from the event object.
		struct EventPolicy
		{
			static EventType getEvent(const EventPointer& event) {
				return event->getType();
			}
		};

		std::cout << std::endl << "EventQueue tutorial 4, typical event system in an application" << std::endl;
		
		using EQ = ECS::EVENT::EventQueueBase<EventType, void(const EventPointer&), EventPolicy>;
		EQ queue;

		queue.appendListener(EventType::mouse, [](const EventPointer& event) {
			const MouseEvent* mouseEvent = static_cast<const MouseEvent*>(event.get());
			std::cout << "Received mouse event, x=" << mouseEvent->getX() << " y=" << mouseEvent->getY()
				<< std::endl;
			});
		queue.appendListener(EventType::keyboard, [](const EventPointer& event) {
			const KeyboardEvent* keyboardEvent = static_cast<const KeyboardEvent*>(event.get());
			std::cout << "Received keyboard event, key=" << (char)keyboardEvent->getKey()
				<< std::endl;
			});
		queue.appendListener(EventType::input, [](const EventPointer& event) {
			std::cout << "Received input event, ";
			if (event->getType() == EventType::mouse) {
				const MouseEvent* mouseEvent = static_cast<const MouseEvent*>(event.get());
				std::cout << "it's mouse event, x=" << mouseEvent->getX() << " y=" << mouseEvent->getY()
					<< std::endl;
			}
			else if (event->getType() == EventType::keyboard) {
				const KeyboardEvent* keyboardEvent = static_cast<const KeyboardEvent*>(event.get());
				std::cout << "it's keyboard event, key=" << (char)keyboardEvent->getKey() << std::endl;
			}
			else {
				std::cout << "it's an event that I don't understand." << std::endl;
			}
			});
		queue.appendListener(EventType::changed, [](const EventPointer& event) {
			const ChangedEvent* changedEvent = static_cast<const ChangedEvent*>(event.get());
			std::cout << "Received changed event, text=" << changedEvent->getText() << std::endl;
			});

		// Asynchronous API, put events in to the event queue.
		queue.enqueue(std::make_shared<MouseEvent>(123, 567));
		queue.enqueue(std::make_shared<KeyboardEvent>('W'));
		queue.enqueue(std::make_shared<ChangedEvent>("This is new text"));
		queue.enqueue(EventType::input, std::make_shared<MouseEvent>(10, 20));
		// then process all events.
		queue.process();

		// Synchronous API, dispatch events to the listeners directly.
		queue.dispatch(std::make_shared<KeyboardEvent>('Q'));
		queue.dispatch(EventType::input, std::make_shared<ChangedEvent>("Should not display"));
	}
	
}

void testDispatch(){
	// The namespace is eventpp
// The first template parameter int is the event type,
// the event type can be any type such as std::string, int, etc.
// The second is the prototype of the listener.
	ECS::EVENT::EventDispatcher<int, void()> dispatcher;

	// Add a listener. As the type of dispatcher,
	// here 3 and 5 is the event type,
	// []() {} is the listener.
	// Lambda is not required, any function or std::function
	// or whatever function object with the required prototype is fine.
	dispatcher.appendListener(3, []() {
		std::cout << "Got event 3." << std::endl;
		});

	auto removeHandle = dispatcher.appendFrontListener(3, []() {
		std::cout << "Got first event 3." << std::endl;
		});

	dispatcher.appendListener(5, []() {
		std::cout << "Got event 5." << std::endl;
		});
	auto handle = dispatcher.appendListener(5, []() {
		std::cout << "Got third event 5." << std::endl;
		});

	dispatcher.insertListener(5, []() {
		std::cout << "Got second event 5." << std::endl;
		},handle);
	
	// Dispatch the events, the first argument is always the event type.
	dispatcher.dispatch(3);
	dispatcher.dispatch(5);

	dispatcher.removeListener(3, removeHandle);
	dispatcher.removeListener(3, removeHandle);
	dispatcher.dispatch(3);

	assertEquals(dispatcher.hasAnyListener(3) == true, "dispatcher.hasAnyListener(3) == true");

	assertEquals(dispatcher.haveHandle(5,handle) == true, "dispatcher.haveHandle(5,handle) == true");

	std::cout << std::endl;
	
	// The listener has two parameters.
	ECS::EVENT::EventDispatcher<int, void(const std::string&, const bool)> dispatcher2;

	dispatcher2.appendListener(3, [](const std::string& s, const bool b) {
		std::cout << std::boolalpha << "Got event 3, s is " << s << " b is " << b << std::endl;
		});
	// The listener prototype doesn't need to be exactly same as the dispatcher.
	// It would be find as long as the arguments is compatible with the dispatcher.
	dispatcher2.appendListener(5, [](std::string s, int b) {
		std::cout << std::boolalpha << "Got event 5, s is " << s << " b is " << b << std::endl;
		});
	dispatcher2.appendListener(5, [](const std::string& s, const bool b) {
		std::cout << std::boolalpha << "Got another event 5, s is " << s << " b is " << b << std::endl;
		});

	// Dispatch the events, the first argument is always the event type.
	dispatcher2.dispatch(3, "Hello", true);
	dispatcher2.dispatch(5, "World", false);

	// Define an Event to hold all parameters.
	struct MyEvent {
		std::string type;
		std::string message;
		int param;
	};

	// Define policies to let the dispatcher knows how to
	// extract the event type.
	struct MyEventPolicies
	{
		static std::string getEvent(const MyEvent& e, bool ) {
			return e.type;
		}
	};

	ECS::EVENT::EventDispatcher<
		std::string,
		void(const MyEvent&, bool),
		MyEventPolicies
	>dispatcher3;

	// Add a listener.
// Note: the first argument is the event type of type int, not MyEvent.

	dispatcher3.appendListener("a", [](const MyEvent& e, bool b) {
		std::cout
			<< std::boolalpha
			<< "Got event 3" << std::endl
			<< "Event::type is " << e.type << std::endl
			<< "Event::message is " << e.message << std::endl
			<< "Event::param is " << e.param << std::endl
			<< "b is " << b << std::endl
			;
		});

	// Dispatch the event.
	// The first argument is Event.
	dispatcher3.dispatch(MyEvent{ "a","Hello world", 38 }, true);
	
}

std::string testCallbackFunction(const bool& flag, const std::string& message){
	return flag ? "Callback 3: " + message : "Callback 3: Condition not met.";
}

struct testCallbackFunctionClass {
	std::string memberFunction(const bool& flag, const std::string& message){
		return flag ? "Callback 4: " + message : "Callback 4: Condition not met.";
	}
};

// テスト関数
void testCallbackList() {

	using CL = ECS::EVENT::CallbackList<std::string(const bool&, const std::string&)>;
	CL callbackList;

	auto handle1 = callbackList.append([](const bool& flag, const std::string& message) -> std::string {
		return flag ? "Callback 1: " + message : "Callback 1: Condition not met.";
		});

	auto handle2 = callbackList.append([](const bool& flag, const std::string& message) -> std::string {
		return flag ? "Callback 2: " + message : "Callback 2: Condition not met.";
		});

	auto handle3 = callbackList.append<&testCallbackFunction>();

	testCallbackFunctionClass testClass;

	auto handle4 = callbackList.append<&testCallbackFunctionClass::memberFunction>(testClass);

	std::string result1 = handle1.lock()->callback(true, "Hello Handle");
	assertEquals(result1 == "Callback 1: Hello Handle", "Callback 1 returned expected value.");

	std::string result2 = handle2.lock()->callback(false, "Hello Handle");
	assertEquals(result2 == "Callback 2: Condition not met.", "Callback 2 returned expected value.");

	std::string result3 = handle3.lock()->callback(false, "Hello Handle");
	assertEquals(result3 == "Callback 3: Condition not met.", "Callback 3 returned expected value.");

	std::string result4 = handle4.lock()->callback(false, "Hello Handle");
	assertEquals(result4 == "Callback 4: Condition not met.", "Callback 4 returned expected value.");

	callbackList.remove(handle2);
	std::cout << "\n--- After Removal Callback2---\n";
	callbackList.foreach([](const CL::CallbackType& callback) {
		std::string result = callback(true, "Hello Callback");
		std::cout<< result << std::endl;
	});

	auto results = callbackList(true,"Hello CallbackVector");
	for(auto&x:results){
		std::cout<<x<<std::endl;
	}
}

void typeListTest()
{
	// テスト
	using MyList = ECS::type_list<int, double, char,float>;
	static_assert(ECS::type_Index_v<int, MyList> == 0, "int should be at index 0");
	static_assert(ECS::type_Index_v<double, MyList> == 1, "double should be at index 1");
	static_assert(ECS::type_Index_v<char, MyList> == 2, "char should be at index 2");
	static_assert(ECS::type_Index_v<float, MyList> != static_cast<std::size_t>(ECS::npos), "float is not in the list");
}

// --- テスト関数 ---
//<Key,Value>
//benchmark(IndexBase_HopscotchHashMap<int,int>(capacity : 8u), "IndexBase");
//benchmark(SparseSetBase_HopscotchHashMap<int, int> sparseSetMap(cap : 100000), "SparseSet");
/*
結果の分析(Debug)
##挿入時間 (IndexBase: 18ms → SparseSet: 15ms)
- SparseSetとほぼ同じレベルまで短縮され、バケット探索のオーバーヘッドが削減されたことがわかる。
- Hopscotchの局所性を活かしつつ、リハッシュの影響を抑えられた ことが成功の要因かも。
##検索時間 (IndexBase: 4ms / SparseSet: 5ms)
- ほぼ差がなく、どちらの方式も高速な検索ができている。
- これは適切なハッシュ計算とバケット配置のおかげ。
##イテレーション時間 (IndexBase: 4ms → SparseSet: 12ms)
- SparseSet方式は dense にデータを密集させるために余計なループ処理が発生しやすいかも。
- 一方、IndexBase方式は next() による空バケットスキップが効率的に機能している。

ankerl::unordered_dense::map - Insert Time: 71 ms
ankerl::unordered_dense::map - Find Time: 47 ms [100000 found]
ankerl::unordered_dense::map - Iteration Time: 11 ms [1409965408 sum]

結果の分析(Release)
##挿入時間 (Insert Time)
- IndexBase: 452ms
- SparseSet: 157ms
SparseSet方式のほうが約3倍高速にデータを挿入 できています。
これは、SparseSetが 連続したメモリアクセスを活用し、キャッシュミスを最小限に抑える ためです。
IndexBase方式では ホップ情報 (hopinfoes_) の管理やバケット移動 (moveEmpty()) が挿入のコストを増やしている 可能性があります。
##検索 (Find Time)
- IndexBase: 237ms
- SparseSet: 279ms
IndexBase方式のほうが若干検索が速い（SparseSetより約42ms短縮）。
これは、IndexBase方式が ホップ情報を活用して、局所性の高い検索ができるため です。
一方、SparseSetは 探索するテーブルが小さいので、検索自体は比較的軽いが、メモリアクセスがやや分散している可能性がある。
##イテレーション (Iteration Time)
- IndexBase: 9ms
- SparseSet: 2ms
SparseSet方式のほうが約4倍高速にイテレーションできている。
SparseSetは dense にデータを格納するため、シーケンシャルアクセスが最適化される のに対し、
IndexBase方式では バケットをスキップしながらイテレーションするため、オーバーヘッドが発生している可能性 があります。
*/

template<typename HashMapType>
void benchmark(HashMapType&, const std::string&, const int);

void hashMapBenchmarks()
{
	struct Health {};
	struct OwnerA {};
	struct OwnerB {};
	struct OwnerC {};

	ECS::ecs_map::HopscotchHashMap<int, int> indexBaseMap(10000000);
	//ecs_map::SparseHopscotchHashMap<int,int> sparseSetMap(10000);
	//ecs_map::unordered_dense_map<int,int> unordered_denseMap(10000);

	std::cout << "Benchmarking...\n";
	benchmark(indexBaseMap, "IndexBase", 10000000);

	//benchmark(sparseSetMap, "SparseSet", 10000);
}

#pragma optimize("", off)  // 最適化を無効化
template<typename HashMapType>
void benchmark(HashMapType& map, const std::string& name, const int numElements) {
	using namespace std::chrono;

	// 挿入テスト
	auto start = high_resolution_clock::now();
	for (int i = 0; i < numElements; ++i) {
		map.insert(i, i * 2);
	}
	auto end = high_resolution_clock::now();
	std::cout << name << " - Insert Time: " << duration_cast<milliseconds>(end - start).count() << " ms\n";	

	// 検索テスト
	start = high_resolution_clock::now();
	int found = 0;
	for (int i = 0; i < numElements; ++i) {
		if (map.find(i)){
			found++;
		}
	}

	end = high_resolution_clock::now();
	std::cout << name << " - Find Time: " << duration_cast<milliseconds>(end - start).count() << " ms ["<< found<<" found"<< "]\n";

	// イテレーションテスト
	start = high_resolution_clock::now();
	volatile long long sum = 0;
	for (auto it = map.begin(); it != map.end(); ++it) {
		sum += *it;
	}
	end = high_resolution_clock::now();
	std::cout << name << " - Iteration Time: " << duration_cast<milliseconds>(end - start).count() << " ms [" << sum <<" sum" << "]\n";
}
#pragma optimize("", on)  // 最適化をオンに戻す

struct testBasicStorageComponent {
	static constexpr ECS::StorageType storage_pref = ECS::StorageType::BasicType;
};

//作成テスト
inline void test_create_group() {
	struct A {};
	struct B {};
	struct C {};
	struct D {};

	auto spawnLog = [](EntityID entt) {
		std::cout << "world spawn " << GetEntityIndex(entt) << " entity" << std::endl;
	};
	
	auto emptyEntity = ECS::world().spawnEmpty();
	auto entity = ECS::world().spawn<A,C,D>();
	spawnLog(entity);
	auto entity2 = ECS::world().spawn<A, B>();
	spawnLog(entity2);
	auto entity3 = ECS::world().spawn<A,B>("Apple");
	spawnLog(entity3);
	auto entity4 = ECS::world().spawn<A,C>();
	spawnLog(entity4);
	auto entity5 = ECS::world().spawn<A,B,C,D>("Banana");
	spawnLog(entity5);
	
	//assertEquals(ECS::world().getComponent<B>(entity)!=nullptr, "ECS::world().getComponent<B>(entity)!=nullptr");
	//assertEquals(ECS::world().getComponent<A>(entity4) != nullptr, "ECS::world().getComponent<A>(entity4)!=nullptr");

	//assertEquals(ECS::COMPONENT::component_storage_selector<A>::value == ECS::StorageType::EventType,"ECS::COMPONENT::component_storage_selector<A>::value == ECS::StorageType::EventType");

	//assertEquals(ECS::COMPONENT::component_storage_selector<testBasicStorageComponent>::value == ECS::StorageType::BasicType, "ECS::COMPONENT::component_storage_selector<testBasicStorageComponent>::value == ECS::StorageType::BasicType");

	//componentPool.

	auto group = ECS::world().group<ECS::StorageType::EventType>(ECS::get<A,B>);
	auto group2 = ECS::world().group<ECS::StorageType::EventType,B>();
	auto group3 = ECS::world().group<ECS::StorageType::EventType,C>();

	auto componentPoolA = group.getComponentPool<A>();

	int index = 0;
	for(auto &x:componentPoolA->GetEntityList()){
		std::cout<<index<<" : " << GetEntityIndex(x)-1 << std::endl;
		index++;
	}

	/*
	auto& componentPool2 = ECS::world().getComponentPool<B>();
	index = 0;
	for (auto& x : componentPool2.GetEntityList()) {
		std::cout
		<< "[" << index << "] " << "B Valid Entity is " << GetEntityIndex(x) << std::endl;
		index++;
	}
	*/

	//ECS::world().removeComponent<A>(entity3);

	/*
	auto entity = ECS::world().spawn<A, B>(A{}, B{});
	auto entity2 = ECS::world().spawn<A, B>();
	auto entity3 = ECS::world().spawn<A, B>("Apple");
	auto entity4 = ECS::world().spawn<A, B>(B{});
	auto entity5 = ECS::world().spawn<A, B>("Banana", B{});
	*/
	//assertEquals(group.node<A>()->Size() != 0 , "Owner::BaseType does not have 'type'!");
	
	/*
	assertEquals(group.count() == 3, "ECS::world().getGroupSize()==3");  // 初期状態ではエンティティなし
	
	assertEquals(group.node<A>() != nullptr, "group.node<A>() != nullptr");
	assertEquals(group.node<C>() != nullptr, "group.node<C>() != nullptr");
	assertEquals(group.node<D>() == nullptr, "group.node<D>() == nullptr");
	std::cout << "node<B>.typename : " <<typeid(group.node<B>()).name() << std::endl;
	assertEquals(group.node<A>()->Size() == 0, "group.node<A>()->Size() == 0");
	ECS::world().spawn("",A{});
	ECS::world().spawn("",A{});
	ECS::world().spawn("",B{});
	assertEquals(group.node<A>()->Size() != 0, "Spawn A valid Entity. group.node<A>()->Size() != 0");
	
	*/
	//assertEquals(group.entityCount<A,B>()[1] == 1, "group.storageSize()[1] == 1");
	
	//group.checkTypeList<owned_t<A>();
}

/*
//追加テスト
void test_add_entity_to_group() {
	Group group;
	Entity entity = create_entity();
	group.add(entity);

	assert(group.contains(entity));  // グループに追加されているかチェック！
}
//除外対象のコンポーネントの適用
void test_exclude_component() {
	Group group;
	Entity entity = create_entity();
	group.add(entity);
	group.exclude<ComponentX>();

	assert(!group.contains(entity));  // 除外設定後に削除されるか確認！
}

//イベント適用
void test_event_trigger() {
	Group group;
	Entity entity = create_entity();

	bool constructed = false, destroyed = false;

	group.on_construct().connect([&](Entity e) { if (e == entity) constructed = true; });
	group.on_destroy().connect([&](Entity e) { if (e == entity) destroyed = true; });

	group.add(entity);
	assert(constructed);  // 構築イベントが発火したかチェック！

	group.remove(entity);
	assert(destroyed);  // 破棄イベントが発火したかチェック！
}
*/

// テスト関数
void testGroupIdentifiers() {
	struct Health {};
	struct OwnerA {};
	struct OwnerB {};
	struct OwnerC {};

	/*
	using GroupA = ECS::Group<owned_t<OwnerA>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupB = ECS::Group<owned_t<OwnerB>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupC = ECS::Group<owned_t<OwnerC>, get_t<Position, Velocity>, exclude_t<>>;
	using GroupD = ECS::Group<owned_t<OwnerA>, get_t<Position>, exclude_t<>>;
	using GroupE = ECS::Group<owned_t<OwnerA>, get_t<Position, Velocity, Health>, exclude_t<>>;

	// 識別 ID を取得
	ecs_map::id_type id_A = GroupA::group_id();
	ecs_map::id_type id_B = GroupB::group_id();
	ecs_map::id_type id_C = GroupC::group_id();
	ecs_map::id_type id_D = GroupD::group_id();
	ecs_map::id_type id_E = GroupE::group_id();

	// 結果を出力
	std::cout << "GroupA ID: " << id_A << std::endl;
	std::cout << "GroupB ID: " << id_B << std::endl;
	std::cout << "GroupC ID: " << id_C << std::endl;
	std::cout << "GroupD ID: " << id_D << std::endl;
	std::cout << "GroupE ID: " << id_E << std::endl;

	// 識別 ID の整合性チェック
	assert(id_A != id_B && "OwnerA と OwnerB のグループ ID が同じ");
	assert(id_A != id_C && "OwnerA と OwnerC のグループ ID が同じ");
	assert(id_A != id_D && "異なるコンポーネントセットなのに同じグループ ID");
	assert(id_A != id_E && "Health コンポーネントがあるのに ID が変わらない");

	std::cout << "すべての識別 ID のテストが正常に完了しました！" << std::endl;
	*/
}

void entityTest()
{
	Position position = Position(0.0f, 5.0f);
	auto entity = ECS::world().spawn("", position, Velocity(5.0f, 0.1f));
	auto entity2 = ECS::world().spawnEmpty();

	//auto entity2 = ECS::sWorld.spawnEmpty();
	ECS::world().emplace<Velocity>(entity2, 1.0f, 0.5f);
	ECS::world().emplace<Position>(entity2);
	//auto comp = ECS::world().getComponent<Position>(entity);

	//auto view2 = view.Exclude<Position>();
	//auto view = ECS::world().View<Velocity>(exclude_t<Position>{});

	/*
	for (size_t i = 0; i < packed.size(); i++) {
		Position a;
		std::tie(a) = packed[i].components;
	}
	*/
	auto view = ECS::world().View<Position, Velocity>();
	//auto view2 = view->Exclude<Position>();

	for (auto& x : *view)
	{
		auto& entityID = x.entity;
		auto& vel = view->get<Velocity>(x.components);
		auto& posi = view->get<Position>(x.components);
		//bool hasComp = ECS::world().has<Velocity>(view->get<EntityID>(x));
	}

	/*
	for (EntityID x : *view2)
	{
		bool hasComp = ECS::world().has<Velocity>(x);

	}
	*/

	for (auto [entityID, position, velocity] : view->each()) {
		auto id = entityID;
		auto posi = position;
		auto vel = velocity;
	}

	view->each([](auto entity, auto& pos, auto& vel) {
		pos.x += 5.0f;
		vel.x += 5.0f;
		});

	view->each([](auto& pos, auto& vel) {
		pos.x += 5.0f;
		vel.x += 5.0f;
		});

	//auto entities = scene.getWorld().findEntitiesWithComponents<Velocity>();
	//auto comp = scene.getWorld().getComponent<Position>(entity);
	//scene.getWorld().removeComponent<Position>(entity);

	//comp->x+= 10.0f;
	//comp = scene.getWorld().getComponent<Position>(entity);

	//scene.getWorld().despawn(entity);
	//scene.getWorld().despawn(entity);
}

bool executeTimeTest()
{
	auto& world = ECS::world();
	auto start_creation = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < 1000000; i++)
	{
		auto entity = world.spawn();
		world.emplace<TransformComponent>(entity, 1.0f, 2.0f, 3.0f);
	}

	auto stop_creation = std::chrono::high_resolution_clock::now();
	auto duration_creation = std::chrono::duration_cast<std::chrono::milliseconds>(stop_creation - start_creation);
	std::cout << "エンティティの作成とコンポーネントの追加: " << duration_creation.count() << " ミリ秒\n";

	auto start_modification = std::chrono::high_resolution_clock::now();

	for (auto [entity, transform] : ECS::world().View<TransformComponent>()->each()) {
		transform._x += 1.0f;
		transform._y += 1.0f;
		transform._z += 1.0f;
	}

	auto stop_modification = std::chrono::high_resolution_clock::now();
	auto duration_modification = std::chrono::duration_cast<std::chrono::milliseconds>(stop_modification - start_modification);
	std::cout << "コンポーネントの変更にかかった時間: " << duration_modification.count() << " ミリ秒\n";

	return true;
}

///////////////////////// 通常のコンポーネント指向////////////////////////////
class NormalComponent {
public:
	virtual void update() = 0;
	virtual ~NormalComponent() {}
};

class NormalTransformComponent : public NormalComponent {
public:
	float _x, _y, _z;

	NormalTransformComponent(float x = 0, float y = 0, float z = 0) : _x(x), _y(y), _z(z) {}

	void update() override {
		_x += 1.0f;
		_y += 1.0f;
		_z += 1.0f;
	}
};

class GameObject {
private:
	std::vector<std::shared_ptr<NormalComponent>> _components;

public:
	GameObject() {}

	template<typename T, typename... Args>
	void addComponent(Args&&... args) {
		_components.push_back(std::make_shared<T>(std::forward<Args>(args)...));
	}

	void update() {
		for (auto& component : _components) {
			component->update();
		}
	}
};

bool executeTimeTest_NormalComponentBase() {
	auto start_creation = std::chrono::high_resolution_clock::now();

	std::vector<GameObject> gameObjects;
	// gameObject(1000000)とするのはフェアじゃなさそうなので1つずつpush_backしてます。
	for (size_t i = 0; i < 1000000; i++)
	{
		GameObject gameObject;
		gameObject.addComponent<NormalTransformComponent>(1.0f, 2.0f, 3.0f);
		gameObjects.push_back(gameObject);
	}
	auto stop_creation = std::chrono::high_resolution_clock::now();
	auto duration_creation = std::chrono::duration_cast<std::chrono::milliseconds>(stop_creation - start_creation);
	std::cout << "GameObjectの作成とコンポーネントの追加: " << duration_creation.count() << " ミリ秒\n";

	auto start_modification = std::chrono::high_resolution_clock::now();

	for (auto& gameObject : gameObjects) {
		gameObject.update();
	}
	auto stop_modification = std::chrono::high_resolution_clock::now();
	auto duration_modification = std::chrono::duration_cast<std::chrono::milliseconds>(stop_modification - start_modification);
	std::cout << "コンポーネントの変更にかかった時間: " << duration_modification.count() << " ミリ秒\n";

	return true;
}
/// /////////////////////////////////////////////

//hash関係計測

// 計測用
using namespace std::chrono;

// FNV-1a ハッシュ
constexpr uint64_t hash_FNV1a(std::string_view sv) {
	constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325;
	constexpr uint64_t FNV_PRIME = 0x100000001b3;

	uint64_t hash = FNV_OFFSET_BASIS;
	for (char c : sv) {
		hash ^= static_cast<uint64_t>(c);
		hash *= FNV_PRIME;
	}
	return hash;
}

// ローリングハッシュ (基数: 31, MOD: 1e9+7)
constexpr uint64_t hash_Rolling(std::string_view sv) {
	constexpr uint64_t BASE = 31;
	constexpr uint64_t MOD = 1000000007;

	uint64_t hash = 0;
	uint64_t power = 1;

	for (char c : sv) {
		hash = (hash * BASE + static_cast<uint64_t>(c)) % MOD;
		power = (power * BASE) % MOD;
	}
	return hash;
}

std::string random_string(size_t length) {
	static std::mt19937_64 rng(std::random_device{}());
	static std::uniform_int_distribution<int> dist(97, 122);  // 'a' ~ 'z'

	std::string s;
	s.reserve(length);
	for (size_t i = 0; i < length; ++i) {
		s += static_cast<char>(dist(rng));  // キャストを追加
	}
	return s;
}

void hashTimetest() {
	constexpr size_t TEST_CASES = 100000;  // テスト回数
	constexpr size_t STRING_LENGTH = 50;   // 文字列長

	std::vector<std::string> test_strings;
	for (size_t i = 0; i < TEST_CASES; ++i) {
		test_strings.push_back(random_string(STRING_LENGTH));
	}

	// FNV-1a ハッシュの測定
	auto start_FNV1a = high_resolution_clock::now();
	for (const auto& str : test_strings) {
		volatile uint64_t hash = hash_FNV1a(str);
	}
	auto end_FNV1a = high_resolution_clock::now();
	auto duration_FNV1a = duration_cast<microseconds>(end_FNV1a - start_FNV1a).count();

	// ローリングハッシュの測定
	auto start_Rolling = high_resolution_clock::now();
	for (const auto& str : test_strings) {
		volatile uint64_t hash = hash_Rolling(str);
	}
	auto end_Rolling = high_resolution_clock::now();
	auto duration_Rolling = duration_cast<microseconds>(end_Rolling - start_Rolling).count();

	// 結果出力
	std::cout << "FNV-1a Hash Time: " << duration_FNV1a << " s\n";
	std::cout << "Rolling Hash Time: " << duration_Rolling << " s\n";

	//FNV - 1a: 17, 445
	//Rolling Hash : 62, 311
}



