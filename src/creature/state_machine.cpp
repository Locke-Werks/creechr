#include "creature/state_machine.h"
#include "util/logging.h"

namespace cr {

StateMachine::StateMachine() = default;
StateMachine::~StateMachine() = default;

void StateMachine::registerState(std::unique_ptr<State> state)
{
    if (!state) {
        return;
    }
    const QString n = state->name();
    m_states.insert_or_assign(n, std::move(state));
}

QString StateMachine::currentName() const
{
    return m_current ? m_current->name() : QString();
}

void StateMachine::changeTo(const QString& name, Creechr& cr, const WorldContext& world)
{
    auto it = m_states.find(name);
    if (it == m_states.end()) {
        LOG_WARN(QStringLiteral("state machine: unknown state '%1', staying").arg(name));
        return;
    }
    State* next = it->second.get();
    if (next == m_current) {
        return;
    }
    if (m_current) {
        m_current->exit(cr, world);
    }
    LOG_DEBUG(QStringLiteral("state: %1 -> %2")
        .arg(m_current ? m_current->name() : QStringLiteral("(none)"))
        .arg(name));
    m_current = next;
    m_current->enter(cr, world);
}

void StateMachine::tick(int deltaMs, Creechr& cr, const WorldContext& world)
{
    if (!m_current) {
        return;
    }
    const QString next = m_current->tick(deltaMs, cr, world);
    if (!next.isEmpty() && next != m_current->name()) {
        changeTo(next, cr, world);
    }
}

} // namespace cr
