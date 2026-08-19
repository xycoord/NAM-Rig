#include "Architectures.h"

#include <mutex>

#include "NAM/container.h"
#include "NAM/convnet.h"
#include "NAM/linear.h"
#include "NAM/lstm.h"
#include "NAM/model_config.h"
#include "NAM/wavenet/model.h"

namespace namrig::engine
{

// The NAM core registers its architectures via file-local static
// initializers (e.g. `static ConfigParserHelper _register_WaveNet(...)`).
// Those objects are silently DROPPED whenever the core is archived into a
// static library and nothing references their translation units — which is
// exactly how juce_add_plugin links us into the final binaries. The symptom
// is "No config parser registered for architecture: X" at model-load time.
//
// So we register explicitly, guarded for the case where the statics did
// survive (direct object linking, e.g. the test binary). Update this list
// when the core adds an architecture — the registration lines live in its
// *.cpp files (grep for ConfigParserHelper).
void registerBuiltinArchitectures()
{
    static std::once_flag once;
    std::call_once(once, [] {
        auto& registry = nam::ConfigParserRegistry::instance();
        auto add = [&registry](const char* name, nam::ConfigParserFunction func) {
            if (!registry.has(name))
                registry.registerParser(name, std::move(func));
        };

        add("Linear", nam::linear::create_config);
        add("LSTM", nam::lstm::create_config);
        add("ConvNet", nam::convnet::create_config);
        add("WaveNet", nam::wavenet::create_config);
        add("SlimmableContainer", nam::container::create_config);
    });
}

} // namespace namrig::engine
