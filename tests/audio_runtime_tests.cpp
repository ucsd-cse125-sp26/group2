#include "client/sfx/AudioRuntime.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{

void testDefaultManifestResolvesEvents()
{
    audio::AudioRuntime runtime;
    runtime.loadDefaultManifest();
    const audio::AudioObjectId object = audio::objectId("player.7");
    runtime.setObjectTransform(object, {10.0f, 20.0f, 30.0f}, {1.0f, 0.0f, 0.0f});

    const auto commands = runtime.postEvent("weapon.rifle.fire", object);
    assert(commands.size() == 1);
    assert(commands[0].type == audio::AudioCommandType::Play);
    assert(commands[0].sfx == SfxId::RifleFire);
    assert(commands[0].positional);
    assert(commands[0].position.x == 10.0f);
    assert(commands[0].priority > 2.0f);

    const auto railgunCommands = runtime.postEvent("weapon.railgun.fire", object);
    assert(railgunCommands.size() == 1);
    assert(railgunCommands[0].sfx == SfxId::RailGunFire);

    const auto shotgunCommands = runtime.postEvent("weapon.shotgun.fire", object);
    assert(shotgunCommands.size() == 1);
    assert(shotgunCommands[0].sfx == SfxId::ShotgunFire);

    const auto beamLoopCommands = runtime.postEvent("weapon.energy.loop", object);
    assert(beamLoopCommands.size() == 1);
    assert(beamLoopCommands[0].sfx == SfxId::EnergyBeamLoop);
    assert(beamLoopCommands[0].loop);
    assert(beamLoopCommands[0].positional);
}

void testFootstepEventUsesConcreteRandom()
{
    audio::AudioRuntime runtime;
    runtime.loadDefaultManifest();
    const audio::AudioObjectId object = audio::objectId("runner");
    runtime.setObjectTransform(object, {0.0f, 0.0f, 0.0f});
    runtime.setRtpc(object, audio::rtpcId("movement.intensity"), 0.75f);

    const auto commands = runtime.postEvent("footstep", object);
    assert(commands.size() == 1);
    assert(static_cast<int>(commands[0].sfx) >= static_cast<int>(SfxId::ConcreteFootstep01));
    assert(static_cast<int>(commands[0].sfx) <= static_cast<int>(SfxId::ConcreteFootstep17));
    assert(commands[0].positional);
}

void testTomlManifestLoadsSwitchRandomAndStop()
{
    const std::string path = "audio_runtime_test_manifest.toml";
    {
        std::ofstream out(path);
        out << R"(
[[busses]]
id = "Master"
volume = 1.0

[[busses]]
id = "Weapons"
parent = "Master"
volume = 0.5
max_voices = 4

[[clips]]
id = "a"
sfx = "RifleFire"
bus = "Weapons"
spatial = true
priority = 2.0

[[clips]]
id = "b"
sfx = "RocketFire"
bus = "Weapons"
spatial = true
priority = 2.0
full_gain_distance = 111.0
max_distance = 1234.0

[[nodes]]
id = "node.a"
type = "sound"
clip = "a"

[[nodes]]
id = "node.b"
type = "sound"
clip = "b"

[[nodes]]
id = "node.switch"
type = "switch"
switch = "weapon.kind"
default = "node.a"
children = [
  { node = "node.b", switch = "rocket" },
]

[[nodes]]
id = "node.state"
type = "switch"
state = "game.phase"
default = "node.a"
children = [
  { node = "node.b", switch = "combat" },
]

[[events]]
id = "weapon.fire"
actions = [{ type = "play", target = "node.switch" }]

[[events]]
id = "phase.fire"
actions = [{ type = "play", target = "node.state" }]

[[events]]
id = "weapon.stop"
actions = [{ type = "stop", target = "node.switch" }]
)";
    }

    audio::AudioRuntime runtime;
    std::vector<std::string> errors;
    assert(runtime.loadManifest(path, &errors));
    const audio::AudioObjectId object = audio::objectId("object");
    runtime.setObjectTransform(object, {2.0f, 0.0f, 0.0f});

    auto commands = runtime.postEvent("weapon.fire", object);
    assert(commands.size() == 1);
    assert(commands[0].sfx == SfxId::RifleFire);
    assert(runtime.busGain(audio::busId("Weapons")) == 0.5f);

    runtime.setSwitch(object, audio::switchGroupId("weapon.kind"), audio::switchValueId("rocket"));
    commands = runtime.postEvent("weapon.fire", object);
    assert(commands.size() == 1);
    assert(commands[0].sfx == SfxId::RocketFire);
    assert(commands[0].maxBusInstances == 4);
    assert(commands[0].fullGainDistance == 111.0f);
    assert(commands[0].silentDistance == 1234.0f);

    commands = runtime.postEvent("phase.fire", object);
    assert(commands.size() == 1);
    assert(commands[0].sfx == SfxId::RifleFire);
    runtime.setState(audio::stateGroupId("game.phase"), audio::stateValueId("combat"));
    commands = runtime.postEvent("phase.fire", object);
    assert(commands.size() == 1);
    assert(commands[0].sfx == SfxId::RocketFire);

    commands = runtime.postEvent("weapon.stop", object);
    assert(commands.size() >= 1);
    assert(commands[0].type == audio::AudioCommandType::StopClip);

    std::remove(path.c_str());
}

} // namespace

int main()
{
    static_assert(audio::stableHash("weapon.rifle.fire") == audio::stableHash("weapon.rifle.fire"));
    testDefaultManifestResolvesEvents();
    testFootstepEventUsesConcreteRandom();
    testTomlManifestLoadsSwitchRandomAndStop();
    return 0;
}
