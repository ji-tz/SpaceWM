#pragma once
#include <QQueue>

// One physical Win hold. Preparation owns the first event; subsequent events
// wait in arrival order until its images are ready.
class WinPreviewEvents {
public:
    enum class Event { WinDown, TabDown, WinUp };
    enum class Result { None, Prepare, Open, Discard };
    Result push(Event event) {
        if (!m_active) {
            if (event != Event::WinDown)
                return Result::None;
            m_active = true;
            return Result::Prepare;
        }
        if (event != Event::WinDown)
            m_events.enqueue(event);
        return Result::None;
    }
    Result complete() {
        if (!m_active || m_events.isEmpty())
            return Result::None;
        const auto event = m_events.dequeue();
        reset();
        return event == Event::TabDown ? Result::Open : Result::Discard;
    }
    void reset() { m_events.clear(); m_active = false; }
    bool active() const { return m_active; }
private:
    QQueue<Event> m_events;
    bool m_active = false;
};
