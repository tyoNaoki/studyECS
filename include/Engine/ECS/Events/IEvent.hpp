#pragma once
#include <memory>

namespace ECS::EVENT {

	enum class WorldEventType
	{
		Collision,
		Damage,
		Spawn,
		Destroy,
		// ...•K—v‚É‰ž‚¶‚Ä’Ç‰Á
	};

class IEvent
{
public:
	explicit IEvent(const WorldEventType type) : type(type) {
	}

	virtual ~IEvent() {
	}

	WorldEventType getType() const {
		return type;
	}

private:
	WorldEventType type;
};

using EventPointer = std::shared_ptr<IEvent>;

struct EventPolicy
{
	static WorldEventType getEvent(const EventPointer& event) {
		return event->getType();
	}
};
}