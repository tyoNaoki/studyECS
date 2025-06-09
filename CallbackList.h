#ifndef ECS_EVENTCALLBACKLIST_HPP
#define ECS_EVENTCALLBACKLIST_HPP

#include <mutex>
#include <string>
#include "Event.hpp"

namespace ECS {

namespace EVENT {

template<typename, typename...>
struct Node;

template<typename, typename...>
class CallbackList;

template<typename ReturnType, typename... Args>
class CallbackList<ReturnType(Args...)> {
public:
	using CallbackType = std::function<ReturnType(Args...)>;

	struct Node;
	using NodePtr = std::shared_ptr<Node>;
	using Handle = std::weak_ptr<Node>;

	struct Node {
		CallbackType callback;
		NodePtr next;
		NodePtr prev;

		explicit Node(const CallbackType& cb) : callback(std::move(cb)) {}
	};

	Handle append(const CallbackType& callback) {
		auto newNode = std::make_shared<Node>(callback);

		std::lock_guard<std::mutex> lock(mutex);
		if (tail) {
			tail->next = newNode;
			newNode->prev = tail;
			tail = newNode;
		}
		else {
			head = newNode;
			tail = newNode;
		}

		return Handle(newNode);
	}

	Handle appendFront(const CallbackType& callback)
	{
		auto newNode = std::make_shared<Node>(callback);

		std::lock_guard<std::mutex> lock(mutex);

		if (head) {
			newNode->next = head;
			head->prev = newNode;
			head = newNode;
		}
		else {
			head = newNode;
			tail = newNode;
		}

		return Handle(newNode);
	}

	Handle insert(const CallbackType& callback, const Handle& before)
	{
		auto beforeNode = before.lock();
		if (beforeNode) {
			auto newNode = std::make_shared<Node>(callback);

			std::lock_guard<std::mutex> lock(mutex);

			newNode->prev = beforeNode->prev;
			newNode->next = beforeNode;

			if (beforeNode->prev)
			{
				beforeNode->prev->next = newNode;
			}

			beforeNode->prev = newNode;

			if (beforeNode == head) {
				head = newNode;
			}

			return Handle(newNode);
		}

		return append(callback);
	}

	bool remove(const Handle& handle) {
		auto handleNode = handle.lock();
		if (!handleNode) return false;

		auto prevNode = handleNode->prev;
		auto nextNode = handleNode->next;

		if (prevNode) {
			prevNode->next = nextNode;
		}
		else {
			head = nextNode;
		}

		if (nextNode) {
			nextNode->prev = prevNode;
		}
		else
		{
			tail = prevNode;
		}

		return true;
	}

	void foreach(const std::function<void(const Handle&, const CallbackType&)>& func)const
	{
		auto current = head;
		while (current) {
			func(current, current->callback);
			current = current->next;
		}
	}

	void foreach(const std::function<void(const CallbackType&)>& func)const {
		auto current = head;
		while (current) {
			func(current->callback);
			current = current->next;
		}
	}

	template<typename... U>
	void operator()(U&&... args) const {
		static_assert(sizeof...(U) == sizeof...(Args),
			"Mismatch in number of arguments.");
		std::lock_guard<std::mutex> lock(mutex);
		auto current = head;
		while (current) {
			// ここで U&&... を使えば、渡された引数の値カテゴリ（lvalue or rvalue）がそのま伝えられる
			current->callback(std::forward<U>(args)...);
			current = current->next;
		}
	}

	bool haveHandle(const Handle& handle)const{
		std::lock_guard<std::mutex> lockGuard(mutex);
		auto handleNode = handle.lock();

		if (handleNode) {
			while (handleNode->prev) {
				handleNode = handleNode->prev;
			}
			return handleNode == head;
		}

		return false;
	}

	operator bool() const {
		return !empty();
	}

	bool empty()const{
		return !head;
	}

	/*
	void operator()(Args&&... args) const{
		std::lock_guard<std::mutex> lock(mutex);
		auto current = head;
		while(current){
			current->callback(std::forward<Args>(args)...);
			current = current->next;
		}
	}
	*/

private:
	NodePtr head = nullptr;
	NodePtr tail = nullptr;
	mutable std::mutex mutex;
};

}//namespace EVENT
}//namespace ECS
#endif