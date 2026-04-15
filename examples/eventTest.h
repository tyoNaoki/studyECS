#pragma once

#include "TestFramework.hpp"
//EVENT TEST
#include "Engine\ECS\Events\EventQueue.hpp"
#include "Engine\ECS\Events\Signal.hpp"

int test()
{
	RUN_TEST("testCallbackList", 1);
	//RUN_TEST("test_particalJobSystem", 450);

	//RUN_TEST("test_bigJobSystem",1);
	//RUN_TEST("test_bigVoidJobSystem",1);
	//RUN_PRIORITY_TESTS(false);

	return 0;
}

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

bool myCallback(const int a, const std::unique_ptr<int>& b) {
	return a < *b;
}

bool myFallback() {
	return false;
}

// テスト関数
TEST_CASE(testCallbackList) {

	using CL = ECS::EVENT::CallbackList_Multi<std::string(const bool&, const std::string&)>;
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
	ASSERT(result1 == "Callback 1: Hello Handle", "Callback 1 returned expected value.");

	std::string result2 = handle2.lock()->callback(false, "Hello Handle");
	ASSERT(result2 == "Callback 2: Condition not met.", "Callback 2 returned expected value.");

	std::string result3 = handle3.lock()->callback(false, "Hello Handle");
	ASSERT(result3 == "Callback 3: Condition not met.", "Callback 3 returned expected value.");

	std::string result4 = handle4.lock()->callback(false, "Hello Handle");
	ASSERT(result4 == "Callback 4: Condition not met.", "Callback 4 returned expected value.");

	callbackList.remove(handle2);
	std::cout << "\n--- After Removal Callback2---\n";
	callbackList.foreach([](const CL::CallbackType& callback) {
		std::string result = callback(true, "Hello Callback");
		std::cout << result << std::endl;
		});

	auto results = callbackList(true, "Hello CallbackVector");
	for (auto& x : results) {
		std::cout << x << std::endl;
	}
}

//SignalEventDispatherのテスト
TEST_CASE(world_event_test)
{
	struct Apple {
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

	const std::unique_ptr<const Banana> cb = std::make_unique<Banana>(10, 10);
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

//Signalのwaitイベントテスト
TEST_CASE(test_wait_unblocks_on_event) {
	std::atomic<bool> unblocked{ false };
	struct Apple {
		int x = 5;
		int y = 0;
	};

	struct Banana {
		int x = 30;
		int y = 0;
	};

	ECS::EVENT::Signal bus;

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
	bus.publish(&a);
	bus.dispatch();

	// 4) worker が目覚めるまで少し待機
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	assert(unblocked == true);

	worker.join();
	std::cout << "test_wait_unblocks_on_event: OK\n";
}

TEST_CASE(test_waitFor_times_out_and_succeeds) {
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
	ECS::EVENT::Signal bus;

	// 1) 空のままで waitFor がタイムアウトすること
	auto t0 = high_resolution_clock::now();
	bool got = false;
	got = bus.waitFor(milliseconds(200));
	auto dt = duration_cast<milliseconds>(high_resolution_clock::now() - t0);
	ASSERT(got == false,"got == true");

	std::cout << "waitFor timeout: OK ( waited "
		<< dt.count() << " ms )\n";

	// 2) イベント到着後ならすぐ戻ること
	//    先に publish しておく
	bus.publish(&a);

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

TEST_CASE(testEventQueue)
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

		queue.processIf([](const int a, const std::unique_ptr<int>& b) {
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


TEST_CASE(testDispatch) {
	// The namespace is eventpp
// The first template parameter int is the event type,
// the event type can be any type such as std::string, int, etc.
// The second is the prototype of the listener.
	ECS::EVENT::EventDispatcher_Multi<int, void()> dispatcher;

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
		}, handle);

	// Dispatch the events, the first argument is always the event type.
	dispatcher.dispatch(3);
	dispatcher.dispatch(5);

	dispatcher.removeListener(3, removeHandle);
	dispatcher.removeListener(3, removeHandle);
	dispatcher.dispatch(3);

	ASSERT(dispatcher.hasAnyListener(3) == true, "dispatcher.hasAnyListener(3) == true");

	ASSERT(dispatcher.haveHandle(5, handle) == true, "dispatcher.haveHandle(5,handle) == true");

	std::cout << std::endl;

	// The listener has two parameters.
	ECS::EVENT::EventDispatcher_Multi<int, void(const std::string&, const bool)> dispatcher2;

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
		static std::string getEvent(const MyEvent& e, bool) {
			return e.type;
		}
	};

	ECS::EVENT::EventDispatcher_Multi<
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

std::string testCallbackFunction(const bool& flag, const std::string& message) {
	return flag ? "Callback 3: " + message : "Callback 3: Condition not met.";
}

struct testCallbackFunctionClass {
	std::string memberFunction(const bool& flag, const std::string& message) {
		return flag ? "Callback 4: " + message : "Callback 4: Condition not met.";
	}
};

