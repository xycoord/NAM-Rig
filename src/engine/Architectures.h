#pragma once

namespace namrig::engine
{
// Idempotent; call before any nam::get_dsp. ModelSlot's constructor does.
void registerBuiltinArchitectures();
} // namespace namrig::engine
