#ifndef ECS_EVENTDISPATCHER_HPP
#define ECS_EVENTDISPATCHER_HPP

#include "CallbackList.hpp"
#include <map>

namespace ECS {

namespace EVENT {
template<
	typename EventType,
	typename Prototype,
	typename Policies = void
	>
class EventDispatcher;

template<typename EventType, typename ReturnType, typename... Args, typename Policies>
class EventDispatcher<EventType, ReturnType(Args...), Policies> {
public:
	using CallBacks = CallbackList<ReturnType(Args...)>;
	using CallbackType = std::function<ReturnType(Args...)>;
	using Handle = typename CallBacks::Handle;
	
	auto appendListener(const EventType& e, const CallbackType& callback) {
		return listeners[e].append(callback);
	}

	auto appendFrontListener(const EventType& e, const CallbackType& callback){
		return listeners[e].append_Front(callback);
	}

	auto insertListener(const EventType& e, const CallbackType& callback, const Handle before){
		return listeners[e].insert(callback,before);
	}

	bool removeListener(const EventType& event, const Handle& handle){
		if (listeners.find(event) != listeners.end()) {
			return listeners[event].remove(handle); // ë∂ç›Ç∑ÇÈèÍçáÇÕçÌèú
		}

		return false;
	}

	template<typename T>
	void dispatch(T&& first, Args... args) const {
		using GetEvent = typename SelectGetEvent<Policies, EventType,HasFunctionGetEvent<Policies, T&&, Args...>::value>::Type;

		const EventType& event = GetEvent::getEvent(std::forward<T>(first), args...);

		auto it = listeners.find(event);
		if (it != listeners.end()) {
			it->second(std::forward<Args>(args)...);
		}
	}

	void dispatch(Args... args) const {
		if constexpr (!std::is_same_v<Policies, void>) {
			using GetEvent = typename SelectGetEvent<Policies, EventType,HasFunctionGetEvent<Policies, Args...>::value>::Type;

			const EventType& event = GetEvent::getEvent(args...);
			auto it = listeners.find(event);

			if (it != listeners.end()) {
				it->second(std::forward<Args>(args)...);
			}
			return;
		}
	}

	bool hasAnyListener(const EventType&event) const{
		auto it = listeners.find(event);
		if (it != listeners.end()) {
			return !it->second.empty();
		}
		return false;
	}

	bool haveHandle(const EventType& event, const Handle& handle) const {
		auto it = listeners.find(event);
		if (it != listeners.end()) {
			return it->second.haveHandle(handle);
		}
		return false;
	}

protected:
	std::map<EventType, CallBacks> listeners;
};

}//namespace EVENT
}//namespace ECS
#endif
