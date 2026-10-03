#pragma once

#include "test/mock/inputleap/MockEventQueue.h"
#include "base/EventQueueTimer.h"
#include <map>

namespace inputleap {

// Deterministic timer delivery; no sleeps, background loop, or live clipboard.
class ClipboardTimerPump {
public:
    explicit ClipboardTimerPump(MockEventQueue& events)
    {
        ON_CALL(events, newOneShotTimer(testing::_, testing::_))
            .WillByDefault([](double, const EventTarget*) { return new EventQueueTimer; });
        ON_CALL(events, add_handler(EventType::TIMER, testing::_, testing::_))
            .WillByDefault([state = handlers](EventType, const EventTarget* target,
                                 const IEventQueue::EventHandler& handler) { (*state)[target] = handler; });
        ON_CALL(events, remove_handler(EventType::TIMER, testing::_))
            .WillByDefault([state = handlers](EventType, const EventTarget* target) { state->erase(target); });
        ON_CALL(events, deleteTimer(testing::_))
            .WillByDefault([](EventQueueTimer* timer) { delete timer; });
    }
    bool fire()
    {
        if (handlers->empty()) return false;
        auto target = handlers->begin()->first;
        auto callback = handlers->begin()->second;
        callback(Event(EventType::TIMER, target));
        return true;
    }
    bool empty() const { return handlers->empty(); }
private:
    using Handlers = std::map<const EventTarget*, IEventQueue::EventHandler>;
    std::shared_ptr<Handlers> handlers = std::make_shared<Handlers>();
};
}
