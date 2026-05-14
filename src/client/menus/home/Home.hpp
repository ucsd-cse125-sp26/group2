#include "IScreen.hpp"
#include "menus/home/ui/HomeUI.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <optional>
#include <string>

struct JoinRequest
{
    std::string serverIp;
    uint16_t serverPort;
};
class Home : public IScreen
{
public:
    bool init(NewRenderer* rendererPtr, SDL_Window* windowPtr);
    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;
    std::optional<JoinRequest> consumeJoinRequest();
    void setJoinError(const std::string& error);

private:
    NewRenderer* renderer = nullptr; ///< Renderer; not owned.
    SDL_Window* window = nullptr;    ///< Application window; not owned.
    JoinMenuState joinMenuState;     ///< State for the join menu UI.
    std::optional<JoinRequest>
        pendingJoinRequest;          ///< Set when the user clicks "Join", cleared on App transition to Lobby.
    std::string joinError;           ///< Optional error message to display on the join menu (e.g. connection failure).
};
