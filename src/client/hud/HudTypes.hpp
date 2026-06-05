/// @file HudTypes.hpp
/// @brief Shared types for the HUD system.

#pragma once

#include "network/MatchStatus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <string>

class InputBindings;
/// Opaque enum declaration — value 0 is KeyboardMouse (see config/InputBindings.hpp).
/// Lets HudGameState store the active device without pulling in the full header.
enum class BindingDevice : std::uint8_t;

// ── Colors ──────────────────────────────────────────────────────────────────

/// @brief RGBA color for HUD elements (linear space, straight alpha).
struct HudColor
{
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;

    constexpr HudColor() = default;
    constexpr HudColor(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr HudColor white() { return {1, 1, 1, 1}; }
    static constexpr HudColor black() { return {0, 0, 0, 1}; }
    static constexpr HudColor red() { return {1, 0, 0, 1}; }
    static constexpr HudColor green() { return {0, 1, 0, 1}; }
    static constexpr HudColor yellow() { return {1, 1, 0, 1}; }
    static constexpr HudColor cyan() { return {0, 1, 1, 1}; }
    static constexpr HudColor transparent() { return {0, 0, 0, 0}; }
};

// ── Enums ───────────────────────────────────────────────────────────────────

enum class HudAnchor
{
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

enum class HudAlign
{
    Left,
    Center,
    Right,
};

/// @brief Icon identifiers for the HUD icon atlas.
enum class HudIcon : uint8_t
{
    None = 0,
    AbilityBarBack,
    AbilityBarFront,
    AbilityIconFrame,
    BulletCountBox,
    GrenadeBox,
    HealthFrameBack,
    HealthFrameFront,
    LetterButton,
    LevelBarBack,
    LevelBarFront,
    NoIcon,
    Radar,
    Scoreboard,
    EnemyDiamond,
    Fall,
    DashAbilityIcon,
    Grapple,
    GrappleAbilityIcon,
    Gravity,
    GravityAbilityIcon,
    LevitateAbilityIcon,
    Grenade,
    FragGrenadeIcon,
    MolotovGrenadeIcon,
    StickyGrenadeIcon,
    Headshot,
    Hp,
    PlayerArrow,
    Shield,
    Skull,
    Tactical,
    WallhackAbilityIcon,
    RecallAbilityIcon,
    TopDecal,
    BottomDecal,
    WeaponFrame,
    ARIcon,
    RocketLauncherIcon,
    RailGunIcon,
    TrackingGunIcon,
};

/// @brief Visual category for generic transient HUD popup messages.
enum class HudPopupKind : std::uint8_t
{
    Info,         ///< Neutral informational popup.
    Success,      ///< Positive confirmation popup.
    Warning,      ///< Warning or attention popup.
    PlayerJoined, ///< Player joined the active match.
    PlayerLeft    ///< Player left the active match.
};

// ── Vertex ──────────────────────────────────────────────────────────────────

/// @brief Per-vertex data for all HUD geometry (48 bytes).
struct HudVertex
{
    float position[2];  ///< Pixel coordinates, origin top-left.
    float uv[2];        ///< Texture coords (atlas UV or local quad pos for shapes).
    float color[4];     ///< RGBA, straight alpha.
    float texMode;      ///< 0=solid, 1=SDF text, 2=sprite, 3=SDF rounded rect, 6=SDF erase mask.
    float shapeData[3]; ///< Mode 3 only: [halfWidth, halfHeight, cornerRadius] in pixels.
};

// ── Crosshair ───────────────────────────────────────────────────────────────

/// @brief Crosshair appearance parameters.
struct CrosshairStyle
{
    float gap = 3.f;                                              ///< Gap from center to start of each line (pixels).
    float length = 6.f;                                           ///< Length of each crosshair arm (pixels).
    float thickness = 2.f;                                        ///< Line thickness (pixels).
    HudColor color{0.168627f, 0.694118f, 0.741176f, 0.85f};       ///< Default primary palette color.
    bool dot = true;                                              ///< Draw center dot.
    float reloadRadius = 20.f;                                    ///< Radius of circular reload indicator (pixels).
    float reloadThickness = 2.f;                                  ///< Thickness of reload indicator (pixels).
    HudColor reloadColor{0.168627f, 0.694118f, 0.741176f, 0.85f}; ///< Color of reload indicator
};

// ── Game State Contract ─────────────────────────────────────────────────────

/// @brief Kill feed entry data.
struct HudKillFeedEntry
{
    std::string killerName;
    std::string victimName;
    int weaponId = 0;
    bool isHeadshot = false;
};

/// @brief Damage direction indicator.
struct HudDamageEvent
{
    float angleDeg = 0.f; ///< Direction the damage came from (degrees, 0=front, CW).
    float amount = 0.f;   ///< Damage amount (for intensity scaling).
};

/// @brief Hit confirmation event.
struct HudHitConfirm
{
    bool isHeadshot = false;
    bool isKill = false;
    bool shieldBreak = false; ///< True when this shot depleted the target's armor.
};

/// @brief Floating damage number spawned at a world-space hit position.
///
/// Projected to screen space each frame by the damage number widget.
struct HudDamageNumber
{
    float worldX = 0.f, worldY = 0.f, worldZ = 0.f; ///< World-space hit position.
    int damage = 0;                                 ///< Damage dealt.
    bool headshot = false;
    bool shielded = false;                          ///< True if target had armor when hit.
};

/// @brief Current damage accumulator state for the local player.
struct HudDamageAccum
{
    int total = 0;                                        ///< Accumulated damage to current target.
    HudColor color{0.168627f, 0.694118f, 0.741176f, 1.f}; ///< Color matching the latest hit type.
};

/// @brief Per-teammate status (for scoreboard / team bar).
struct HudTeamMemberStatus
{
    std::string name;
    HudColor color = HudColor::white();
    int health = 100;
    bool isAlive = true;
    bool isLocal = false;
    int kills = 0;
    int deaths = 0;
    int ping = 0;
};

/// @brief World-space position for minimap display.
struct HudMinimapDot
{
    float worldX = 0.f, worldZ = 0.f;
};

/// @brief World-space enemy entry — for the floating HP bar that hovers above
///        each visible enemy in the world (renders only when on-screen).
struct HudWorldEnemy
{
    float worldX = 0.f, worldY = 0.f, worldZ = 0.f; ///< Top of the enemy capsule.
    std::string name;                               ///< Display label (e.g. "RAIDEN").
    int health = 100, maxHealth = 100;
    int armor = 0, maxArmor = 100;
    bool isAlive = true;
    bool occluded = false; ///< True when world geometry blocks the line of sight from the camera (hide bar/name).
};

/// @brief Killcam killer marker — the killer's world AABB + nickname, used on
/// the death screen to float the killer's name above them while awaiting
/// respawn. (The red wallhack silhouette itself is drawn by the 3D renderer.)
struct HudKillerBox
{
    bool valid = false;          ///< True while the killcam is tracking a killer.
    glm::vec3 center{0.f};       ///< Killer AABB center in world space.
    glm::vec3 halfExtents{0.f};  ///< Killer AABB half-extents in world space.
    std::string name;            ///< Killer's display nickname.
};

/// @brief Equipment slot state — drives the bottom-center grapple/grenade/tactical row.
struct HudEquipmentState
{
    std::string primaryAbilityName = "LOCKED";
    std::string secondaryAbilityName = "LOCKED";
    std::string grenadeName = "FRAG";
    float primaryAbilityCharge = 1.f;   ///< 0..1, 1 = ready.
    float secondaryAbilityCharge = 1.f; ///< 0..1, 1 = ready.
    bool primaryAbilityAvailable = false;
    bool secondaryAbilityAvailable = false;
    bool secondaryAbilityMarked = false;
    float grappleCharge = 1.f;  ///< Legacy fallback; 0..1, 1 = ready.
    float grenadeCharge = 1.f;  ///< 0..1, 1 = ready (used when count not shown).
    float tacticalCharge = 1.f; ///< 0..1, 1 = ready.
    int grenadeCount = 2;       ///< Count for slot showing a number; 0 = unavailable.
    int tacticalCount = 1;      ///< Count for slot showing a number; 0 = unavailable.
};

inline constexpr std::size_t kHudGrenadeSlots = 3;

struct HudGrenadeRadialItem
{
    std::string name;
    int count = 0;
    bool available = true;
};

struct HudGrenadeRadialState
{
    bool open = false;
    int selectedIndex = -1;
    std::array<HudGrenadeRadialItem, kHudGrenadeSlots> items;
};

/// @brief Emote wheel state — drives the radial emote selection menu.
/// Item labels come from the neutral emote catalog (EmoteCatalog.hpp); the
/// widget only needs to know whether the wheel is open and which sector is
/// currently highlighted.
struct HudEmoteWheelState
{
    bool open = false;       ///< True while the Emote binding is held.
    int selectedIndex = -1;  ///< Highlighted emote sector, or -1 (none).
};

struct HudAbilityChoice
{
    std::string name;
    std::string description;
};

struct HudAbilitySelectionState
{
    bool available = false;
    bool modifierHeld = false;
    int level = 0;
    std::string slotLabel;
    std::array<HudAbilityChoice, 2> choices;
};

/// @brief Pickup notification entry — slide-in messages for items just acquired.
struct HudPickupNotification
{
    std::string label; ///< E.g. "PULSE·MAG".
    int qty = 1;       ///< Amount picked up.
};

/// @brief Generic transient HUD popup message.
struct HudPopupMessage
{
    HudPopupKind kind = HudPopupKind::Info; ///< Styling category.
    std::string text;                       ///< Already-formatted display text.
};

/// @brief Local player K/D/A — feeds the top-right counter.
struct HudKdaCounter
{
    int kills = 0;
    int deaths = 0;
    int assists = 0;
};

struct HudChatMessage
{
    std::string senderName;
    std::string message;
    float ageSeconds = 0.f;
    bool fromLocal = false;
};

struct HudChatState
{
    bool open = false;
    std::string draft;
    std::span<const HudChatMessage> messages;
};

struct HudVoiceSpeaker
{
    std::string senderName;
};

/// @brief Match info for the top-center header.
struct HudMatchInfo
{
    float elapsedSeconds = 0.f; ///< Time since match started.
    int fragTarget = 30;        ///< Score-to-win.
    bool valid = false;         ///< False when no match metadata is available.
};

/// @brief Per-pellet result for a single shotgun blast, used by ShotgunPelletWidget.
struct HudShotgunPellet
{
    enum class Result : uint8_t
    {
        Miss = 0,
        Body = 1,
        Head = 2,
    };
    Result result = Result::Miss;
};

/// @brief A single completed shotgun blast (11 pellets) staged for the HUD widget.
/// Index 0 = centre. Indices 1..5 = inner pentagon (×0.5 radius). Indices 6..10
/// = outer pentagon (×1.0 radius). Both pentagons share the same 5 angles
/// (72° spacing, top first). Order MUST match the server's `k_offsets` in
/// WeaponSystem.cpp so widget colours align with the actual rays fired.
struct HudShotgunBlast
{
    bool valid = false;           ///< False = no recent blast; widget stays hidden.
    float secondsSinceFire = 0.f; ///< Drives the widget fade animation.
    std::array<HudShotgunPellet, 11> pellets{};
};

/// @brief Snapshot of game state consumed by the HUD each frame.
///
/// Filled by Game from ECS data. The HUD never imports ECS headers.
struct HudGameState
{
    const InputBindings* bindings = nullptr; ///< Live input bindings for HUD key prompts.
    BindingDevice activeInputDevice{};       ///< Last-used input device; selects KBM vs controller glyphs (0 == KBM).

    int health = 100, maxHealth = 100;
    int armor = 0, maxArmor = 100;
    float abilityLevelProgress = 0.f; ///< accumDamage / systems::dmgThreshold, clamped 0..1.
    int abilityLevel = 0;
    int ammoClip = 30, ammoReserve = 90;
    int weaponId = 0;
    int primaryWeaponId = 0;   ///< WeaponType ordinal in the primary slot.
    int secondarySlotWeaponId = 2; ///< WeaponType ordinal in the secondary slot.
    int activeWeaponSlot = 0;      ///< 0 = primary, 1 = secondary.
    bool railgunScoped = false;    ///< True while the local player holds right click with the railgun equipped.
    float railgunChargeTime = 0.f; ///< Current GunInstance::chargeTime for the equipped railgun.
    float roundTimeRemaining = 0.f;
    bool isAlive = true;
    MatchPhase currentPhase = MatchPhase::IN_PROGRESS; ///< Current server match phase.
    bool matchWon = false;                             ///< True if the local player won the finished match.
    bool forceScoreboardOpen = false;                  ///< True when match flow should show scoreboard automatically.
    bool isReloading = false;
    float reloadProgress = 0.0f;                       ///< 0-1

    // Events (valid for this frame only).
    std::span<const HudKillFeedEntry> killFeedEvents;
    std::span<const HudDamageEvent> damageEvents;
    std::span<const HudHitConfirm> hitConfirms;
    std::span<const HudDamageNumber> damageNumbers; ///< Floating damage numbers to spawn this frame.
    HudDamageAccum damageAccum;                     ///< Running damage total to current target.
    HudShotgunBlast latestShotgunBlast;             ///< Most recent shotgun blast; valid=false when none.

    // View/projection for world→screen projection (damage numbers).
    glm::mat4 viewProj{1.f};

    // Team status.
    std::span<const HudTeamMemberStatus> allies;
    std::span<const HudTeamMemberStatus> enemies;
    int allyScore = 0, enemyScore = 0;

    // Minimap.
    float localPlayerX = 0.f, localPlayerZ = 0.f;
    float localPlayerYaw = 0.f;       ///< Player facing direction (radians, 0 = +Z, CW).
    std::span<const HudMinimapDot> enemyDots;
    float minimapWorldRange = 4000.f; ///< World units visible in each direction from center.

    // Vignette events (set by Game each frame based on health/armor deltas).
    bool tookDamage = false;     ///< True the frame health or armor decreased.
    float damageIntensity = 0.f; ///< 0..1 fraction of max-health lost this frame.
    bool armorBroke = false;     ///< True the frame armor dropped to zero.
    // isAlive already covers death vignette.

    // Weapon pickup prompt — populated by Game when the local player is in
    // range of (and looking at) an available weapon spawner. The HUD reads
    // these to render a "Press <binding> to pick up <Weapon>" hint.
    bool pickupAvailable = false; ///< True when a pickup is currently actionable.
    int pickupWeaponId = 0;       ///< Mirrors WeaponType: 0=Rifle, 1=Rocket, 2=RailGun, 3=EnergyGun.

    // Voidfall HUD additions.
    std::span<const HudWorldEnemy> worldEnemies;                ///< Enemies whose HP bars float above them in-world.
    HudEquipmentState equipment;                                ///< Grapple / grenade / tactical state.
    HudGrenadeRadialState grenadeRadial;                        ///< Held-G grenade selection radial.
    HudEmoteWheelState emoteWheel;                              ///< Held-B emote selection wheel.
    HudKillerBox killerBox;                                    ///< Killcam: red box framing the killer while dead.
    HudAbilitySelectionState abilitySelection;                  ///< Pending level-up ability choice.
    std::span<const HudPickupNotification> pickupNotifications; ///< Slide-in pickup messages this frame.
    std::span<const HudPopupMessage> popupMessages;             ///< Generic popup messages this frame.
    HudKdaCounter kda;                                          ///< Local player kill/assist/death counter (top-right).
    HudChatState chat;                                          ///< Bottom-left all-chat log/input.
    std::span<const HudVoiceSpeaker> voiceSpeakers;             ///< Currently audible proximity voice speakers.
    HudMatchInfo matchInfo;   ///< Match elapsed time + frag target (top-center header).
    int gravityDirection = 0; ///< 0=down, 1=left, 2=up, 3=right (HUD compass arrow).

    // Inactive-slot weapon snapshot (drives the small weapon slot sub-row at
    // the bottom of the weapon panel — i.e. the
    // *other* weapon, whichever the player isn't currently holding).
    int secondaryWeaponId = -1; ///< -1 = no inactive weapon loaded.
    int secondaryClip = 0;      ///< Mag count of the inactive weapon.
    int secondaryReserve = 0;   ///< Reserve count of the inactive weapon.
    int secondaryKeybind = 2;   ///< 1 or 2 — the keybind that swaps to this slot.

    // Magazine capacities — sourced from WeaponConfig.magazineSize each frame
    // so the HUD never hardcodes per-weapon constants.
    int magCapacity = 0;          ///< Max rounds per mag for the current weapon.
    int secondaryMagCapacity = 0; ///< Max rounds per mag for the secondary weapon (when set).

    // Screen dimensions (set by Game each frame).
    float screenW = 1280.f, screenH = 720.f;
};
