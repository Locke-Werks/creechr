// state machine. dead simple: hash of named State objects, one is
// "current", every tick the current one returns the name of the next
// state ("" means stay). enter()/exit() get called on transitions.
//
// new state? subclass State, implement name() and tick(), register it
// with the machine in Creechr's ctor. that's it. no factories, no
// templates, no event types, no transition tables. when you find
// yourself wanting any of those, the answer is "you don't".
#pragma once

#include <QString>
#include <map>
#include <memory>

namespace cr {

class Creechr;
struct WorldContext;

class State
{
public:
    virtual ~State() = default;
    virtual QString name() const = 0;
    virtual void enter(Creechr& /*cr*/, const WorldContext& /*world*/) {}
    virtual void exit(Creechr& /*cr*/, const WorldContext& /*world*/) {}
    // returns the next state name, or empty string to stay in this state.
    virtual QString tick(int deltaMs, Creechr& cr, const WorldContext& world) = 0;
};

class StateMachine
{
public:
    StateMachine();
    ~StateMachine();

    void registerState(std::unique_ptr<State> state);
    void changeTo(const QString& name, Creechr& cr, const WorldContext& world);

    State* current() const { return m_current; }
    QString currentName() const;

    void tick(int deltaMs, Creechr& cr, const WorldContext& world);

private:
    // qhash chokes on move-only values in 6.8 — pretend you're surprised.
    // std::map handles unique_ptr fine and we will never have enough
    // states for the log(N) lookup to matter.
    std::map<QString, std::unique_ptr<State>> m_states;
    State* m_current = nullptr;
};

} // namespace cr
