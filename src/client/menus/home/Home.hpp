/// @file Home.hpp
/// @brief Main menu screen with server join form.

#include "IScreen.hpp"
#include "menus/home/ui/HomeUI.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <optional>
#include <string>

/// @brief Server address and port entered by the user on the home screen.
struct JoinRequest
{
    std::string serverIp; ///< Hostname or IP address of the target server.
    uint16_t serverPort;  ///< TCP port of the target server.
};

/// @brief IScreen implementation for the main menu; hosts the server join form.
class Home : public IScreen
{
public:
    /// @brief Bind renderer and window; must be called before iterate().
    /// @return False if either pointer is null.
    bool init(NewRenderer* rendererPtr, SDL_Window* windowPtr);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    /// @brief Take the pending join request set when the user clicks "Join", clearing it.
    /// @return The request, or nullopt if none is pending.
    std::optional<JoinRequest> consumeJoinRequest();

    /// @brief Display an error string on the join form (e.g. from a failed connection attempt).
    void setJoinError(const std::string& error);

private:
    NewRenderer* renderer = nullptr; ///< Renderer; not owned.
    SDL_Window* window = nullptr;    ///< Application window; not owned.
    JoinMenuState joinMenuState;     ///< Mutable state backing the join form widgets.
    std::optional<JoinRequest>
        pendingJoinRequest;          ///< Set when the user clicks "Join", cleared on App transition to Lobby.
    std::string joinError;           ///< Error message shown on the join form; empty when no error.
};
